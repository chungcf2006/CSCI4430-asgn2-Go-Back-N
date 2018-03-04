#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include "mygbn.h"

struct MYGBN_Packet make_packet(int packetType, unsigned int seqNum, unsigned int payloadLength, unsigned char *payload) {
  struct MYGBN_Packet output;
  output.protocol[0] = 'g';
  output.protocol[1] = 'b';
  output.protocol[2] = 'n';
  output.type = packetType;
  output.seqNum = seqNum;
  output.length = 12 + payloadLength;
  if (payload != NULL)
    memcpy(output.payload, payload, payloadLength);

  return output;
}

void mygbn_init_sender(struct mygbn_sender* mygbn_sender, char* ip, int port, int N, int timeout){
  int sd;

  printf("Getting Socket\n");
  if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    fprintf(stderr, "Socket Error %s (%d)\n", strerror(errno), errno);
    exit(1);
  }
  printf("Socket: %d\n", sd);


  printf("Getting Host IP\n");
  struct hostent *ht = gethostbyname(ip);
  if (ht == NULL) {
		fprintf(stderr, "Hostname Error %s (%d)\n", strerror(errno), errno);
		exit(1);
	}

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  memcpy(&server_addr.sin_addr, ht->h_addr, ht->h_length);

  socklen_t addrLen = sizeof(server_addr);

  printf("Host IP: %s\n", inet_ntoa(server_addr.sin_addr));


  mygbn_sender->sd = sd;
  mygbn_sender->to = server_addr;
  mygbn_sender->toLen = addrLen;
  mygbn_sender->N = N;
  mygbn_sender->send_base = 1;
  mygbn_sender->seqNum = 1;
  mygbn_sender->ack_seqNum = 0;
  mygbn_sender->timeout = timeout;

  mygbn_sender->ack_receiver_thread_init = 0;
  pthread_create(&mygbn_sender->ack_receiver_thread, NULL, mygbn_recv_ack, (void *)mygbn_sender);
  while (!mygbn_sender->ack_receiver_thread_init) {
    sleep(1);
  }
  sleep(5);
  printf("Init Complete\n");
}

// Thread for receiving AckPacket
void *mygbn_recv_ack (void *args) {

  struct mygbn_sender* mygbn_sender = (struct mygbn_sender*)args;
  struct MYGBN_Packet ack_packet;
  mygbn_sender->ack_receiver_thread_init = 1;
  printf("ACK Receiver Thread Started\n");
  while (1) {
    recvfrom(mygbn_sender->sd, &ack_packet, 12, 0, (struct sockaddr *)&mygbn_sender->to, &mygbn_sender->toLen);
    printf("\x1B[34mRECV ACK %d\x1B[37m\n", ack_packet.seqNum);
    if (mygbn_sender->ack_seqNum + 1 == ack_packet.seqNum) {
      mygbn_sender->ack_seqNum++;
      mygbn_sender->send_base++;
    }
  }
}

// Thread for triggering the retransmissions upon timeouts
void *mygbn_trigger_retransmission (void *args) {
  printf("Retransmission Thread Started\n");
  struct mygbn_sender* mygbn_sender = (struct mygbn_sender*)args;
  mygbn_sender->send_base = 0;

  return NULL;
}

int mygbn_send(struct mygbn_sender* mygbn_sender, unsigned char* buf, int len){
  int byte_left = len;
  int payloadLength;
  int buffer_pointer = 0;
  int sent_byte = 0;
  struct MYGBN_Packet data_packet;
  while (byte_left > 0) {
    if ((mygbn_sender->seqNum - mygbn_sender->send_base) < mygbn_sender->N) {
      data_packet.seqNum = mygbn_sender->seqNum;
      payloadLength = MIN(byte_left, MAX_PAYLOAD_SIZE);
      data_packet = make_packet(DataPacket, data_packet.seqNum, payloadLength, &buf[buffer_pointer]);

      printf("\tType: 0x%02X\n", data_packet.type);
      printf("\tSeqNum: %d\n", data_packet.seqNum);
      printf("\tLength: %d\n", data_packet.length);

      sent_byte = sendto(mygbn_sender->sd, &data_packet, 12 + payloadLength, 0, (struct sockaddr *)&mygbn_sender->to, mygbn_sender->toLen);
      printf("Sent %d\n", data_packet.seqNum);

      byte_left -= sent_byte;
      buffer_pointer += sent_byte;
      mygbn_sender->seqNum++;
    }
  }
  return buffer_pointer;
}

void mygbn_close_sender(struct mygbn_sender* mygbn_sender){
  struct MYGBN_Packet end_packet, ack_packet;

  // Send EndPacket to Server
  end_packet = make_packet(EndPacket, mygbn_sender->seqNum, 0, NULL);
  sendto(mygbn_sender->sd, &end_packet, 12, 0, (struct sockaddr *)&mygbn_sender->to, mygbn_sender->toLen);

  // Receive AckPacket for EndPacket
  recvfrom(mygbn_sender->sd, &ack_packet, 12, 0, (struct sockaddr *)&mygbn_sender->to, &mygbn_sender->toLen);

  close(mygbn_sender->sd);
}




void mygbn_init_receiver(struct mygbn_receiver* mygbn_receiver, int port){
  int sd;
  if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    fprintf(stderr, "Socket Error %s (%d)\n", strerror(errno), errno);
    exit(1);
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);

  if (bind(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
    fprintf(stderr, "Bind Error %s (%d)\n", strerror(errno), errno);
    exit(1);
  }

  mygbn_receiver->sd = sd;
  mygbn_receiver->expected_seqNum = 1;
  mygbn_receiver->normalSeq = 0;

  printf("Init complete\n");
}

int mygbn_recv(struct mygbn_receiver* mygbn_receiver, unsigned char* buf, int len){
  int validPacket = 0;
  int recv_len = -1;
  struct MYGBN_Packet recv_packet, ack_packet;
  printf("Start RECV\n");
  recvfrom(mygbn_receiver->sd, &recv_packet, 12+512, 0, (struct sockaddr *)&mygbn_receiver->from, &mygbn_receiver->fromLen);
  printf("\tType: 0x%02X\n", recv_packet.type);
  printf("\tSeqNum: %d\n", recv_packet.seqNum);
  printf("\tLength: %d\n", recv_packet.length);

  printf("Received Sequence Number:%d, Expected: %d\n", recv_packet.seqNum, mygbn_receiver->expected_seqNum);
  if (recv_packet.seqNum == mygbn_receiver->expected_seqNum) {
    validPacket = 1;
    mygbn_receiver->expected_seqNum++;
    mygbn_receiver->normalSeq = recv_packet.seqNum;
    recv_len = recv_packet.length - 12;
  }

  if (validPacket) {
    printf("Valid Packet\n");
    printf("RECV ");
    switch (recv_packet.type) {
      case DataPacket:
        printf("DataPacket (Payload Length = %d)\n", recv_packet.length-12);
        printf("recv_len = %d\n", recv_len);
        memcpy(buf, &recv_packet.payload, recv_len);
      break;
      case EndPacket:
        printf("EndPacket\n");
        mygbn_receiver->expected_seqNum = 0;
      break;
    }

  }

  // Acknowledged
  ack_packet = make_packet(AckPacket, mygbn_receiver->normalSeq, 0, NULL);
  sendto(mygbn_receiver->sd, &ack_packet, 12, 0, (struct sockaddr *)&mygbn_receiver->from, mygbn_receiver->fromLen);
  printf("\x1B[34mSENT ACK %d\x1B[37m\n", mygbn_receiver->normalSeq);
  return recv_len;
}

void mygbn_close_receiver(struct mygbn_receiver* mygbn_receiver) {
  close(mygbn_receiver->sd);
}

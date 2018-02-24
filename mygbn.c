#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "mygbn.h"

void mygbn_init_sender(struct mygbn_sender* mygbn_sender, char* ip, int port, int N, int timeout){
  int sd;
  if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    fprintf(stderr, "Socket Error %s (%d)\n", strerror(errno), errno);
    exit(1);
  }

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

  mygbn_sender->sd = sd;
  mygbn_sender->to = server_addr;
  mygbn_sender->toLen = addrLen;
  mygbn_sender->N = N;
  mygbn_sender->send_base = 0;
  mygbn_sender->timeout = timeout;
}

int mygbn_send(struct mygbn_sender* mygbn_sender, unsigned char* buf, int len){
  int byte_left = len;
  while (byte_left > 0) {
    struct MYGBN_Packet data_packet;
    data_packet.protocol[0] = 'g';
    data_packet.protocol[1] = 'b';
    data_packet.protocol[2] = 'n';
    data_packet.type = DataPacket;
    data_packet.seqNum = mygbn_sender->seqNum;
    data_packet.length = sizeof(data_packet);
    byte_left -= sendto(mygbn_sender->sd, buf, len, 0, (struct sockaddr *)&mygbn_sender->to, mygbn_sender->toLen);
  }
  return len;
}

void mygbn_close_sender(struct mygbn_sender* mygbn_sender){
  struct MYGBN_Packet end_packet, ack_packet;
  end_packet.protocol[0] = 'g';
  end_packet.protocol[1] = 'b';
  end_packet.protocol[2] = 'n';
  end_packet.type = EndPacket;
  end_packet.seqNum = mygbn_sender->seqNum;
  end_packet.length = sizeof(end_packet);

  sendto(mygbn_receiver->sd, &end_packet, 12, 0, (struct sockaddr *)&mygbn_receiver->from, &mygbn_receiver->fromLen);

  recvfrom(mygbn_receiver->sd, &ack_packet, 12, 0, (struct sockaddr *)&mygbn_receiver->from, &mygbn_receiver->fromLen);

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
  mygbn_receiver->currentSeqNum = 0;

  printf("Init complete\n");
}

int mygbn_recv(struct mygbn_receiver* mygbn_receiver, unsigned char* buf, int len){
  int validPacket = 0;
  struct MYGBN_Packet recv_packet, ack_packet;
  recvfrom(mygbn_receiver->sd, &recv_packet, 12, 0, (struct sockaddr *)&mygbn_receiver->from, &mygbn_receiver->fromLen);

  if (recv_packet.seqNum == mygbn_receiver->seqNum + 1) {
    validPacket = 1;
    mygbn_receiver->currentSeqNum++;
  }


  int recv_len;
  switch (recv_packet.type) {
    case DataPacket:
      recv_len = recvfrom(mygbn_receiver->sd, &recv_packet, recv_packet.length-12, 0, (struct sockaddr *)&mygbn_receiver->from, &mygbn_receiver->fromLen);
    break;
    case EndPacket:
      mygbn_receiver->currentSeqNum = 0;
    break;
  }

  if (validPacket) {
    memcpy(buf, recv_packet.payload, recv_len);
  }

  // Acknowledged
  ack_packet.protocol[0] = 'g';
  ack_packet.protocol[1] = 'b';
  ack_packet.protocol[2] = 'n';
  ack_packet.type = AckPacket;
  ack_packet.seqNum = mygbn_receiver->currentSeqNum;
  ack_packet.length = sizeof(ack_packet);
  sendto(mygbn_receiver->sd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&mygbn_receiver->from, &mygbn_receiver->fromLen);

  return recv_len;
}

void mygbn_close_receiver(struct mygbn_receiver* mygbn_receiver) {
  close(mygbn_receiver->sd);
}

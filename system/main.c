/*  main.c  - main */

#include <xinu.h>
#include <stdio.h>

extern uint32 nsaddr;

process	main(void)
{
	pid32	shell_pid;

	/* Start the network */

	netstart();

	nsaddr = 0x800a0c10;

	volatile tcpseq seq1 = 0x1b36e265;
	volatile tcpseq seq2 = seq1;
	volatile int32 datalen = 0, codelen = 1;
	volatile int32 size = 65535;
	kprintf("%d %d\n", SEQ_CMP(seq1+datalen+codelen,seq2+size), SEQ_CMP(seq1+datalen+codelen,seq2+size)>0);
	//while(1);
	kprintf("NSEM: %d, NPROC %d\n", NSEM, NPROC);
	tcp_init();
	uint32 serverip;
	dot2ip("128.10.3.51", &serverip);
	kprintf("tcp_init done\n");
	int32	slot = tcp_register(NetData.ipucast, 12345, 0);
	kprintf("TCP slot %d\n", slot);
	int32 newslot;
	tcp_recv(slot, &newslot, 4);
	kprintf("newslot %d\n", newslot);
	//tcp_send(newslot, "hello\n", 6);
	int32 retval;
	char *buf = getmem(5*1024);
	//while((retval = tcp_recv(newslot, buf, 100)) != 0);
	//sleep(1);
	//while(tcp_recv(newslot, buf, 100) != 0);
	//kprintf("---------------------------------Received a close request from client\n");
	int32	total = 0;
	int32	prev = -1;
	int32	next = 0;
	char *bptr = buf;
	int32 totalrv = 0;
	while(1) {
		int32	rv;
		if((rv = tcp_recv(newslot, bptr, 4096-totalrv)) == 0) {
			tcp_close(newslot);
			kprintf("-------------------------Total data rcvd %d\n", total);
			break;
		}
		else {
			totalrv += rv;
			if(totalrv < 4096) {
				bptr += rv;
				continue;
			}
		}
		//kprintf("--------------------------read from client: %d\n", rv);
		total += totalrv;
		/*int32 j;
		int32 *ptr32 = (int32 *)buf;
		for(j = 0; j < 1024; j++) {
			if((*ptr32) != next) {
				kprintf("expected %d, got %d\n", next, *ptr32);
				panic("!");
			}
			ptr32++; next++;
		}*/
		bptr = buf;
		totalrv = 0;
	}
	//tcp_close(newslot);
	tcp_close(slot);
	kprintf("\n...creating a shell\n");
	recvclr();
	resume(shell_pid = create(shell, 8192, 50, "shell", 1, CONSOLE));

	/* Wait for shell to exit and recreate it */

	while (TRUE) {
		while(shell_pid != receive());
		sleepms(200);
		kprintf("\n\nMain process recreating shell\n\n");
		resume(create(shell, 4096, 20, "shell", 1, CONSOLE));
	}
	return OK;
}

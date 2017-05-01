all: reliable_sender reliable_receiver

reliable_sender:
	gcc -pthread -std=gnu99 -o reliable_sender sender_main.c

reliable_receiver:
	gcc -pthread -std=gnu99 -o reliable_receiver receiver_main.c

clean:
	rm reliable_sender reliable_receiver

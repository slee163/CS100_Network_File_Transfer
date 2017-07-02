COMPILE=g++ -ggdb
PROGRAMS=threadClient tprServer poolServer lfServer

all:$(PROGRAMS)

threadClient: threadClient.cc Timer.h
	$(COMPILE) threadClient.cc -lpthread -o threadClient

tprServer: tprServer.cc
	$(COMPILE) tprServer.cc -lpthread -o tprServer

poolServer: poolServer.cc
	$(COMPILE) poolServer.cc -lpthread -o poolServer

lfServer: lfServer.cc
	$(COMPILE) lfServer.cc -lpthread -o lfServer

clean:
	rm $(PROGRAMS) threadDir*/*; rmdir threadDir*

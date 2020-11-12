#ifndef FS_H
#define FS_H

#include <3ds.h>

//FS_Archive fsArchive, ctrArchive;

u32 crc32(u8 *data, u32 size);
void openArchive(FS_ArchiveID id);
void closeArchive(FS_ArchiveID id);
Result makeDir(FS_Archive archive, const char * path);
bool fileExists(const char * path);
bool fileExistsNand(const char * path);
bool dirExists(FS_Archive archive, const char * path);
u64 getFileSize(const char * path);
Result readFile(const char * path, void * buf, u32 size);
Result writeFile(const char * path, void * buf, u32 size);
Result copy_file(char * old_path, char * new_path);
Result delete_file(const char * path);


#endif
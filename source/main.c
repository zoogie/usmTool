#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <inttypes.h>
#include <curl/curl.h>
#include <3ds.h>
#include "checksum.h"
#include "slot1.h"
#include "cfgu.h"
#include "fs.h"
#include "curl.h"

u8 workbuf[0xC00]={0};
u8 *usmlist;
const char *yellow="\x1b[33;1m";
const char *blue="\x1b[34;1m";
const char *dblue="\x1b[34;0m";
const char *white="\x1b[37;1m";
#define HAXX 0x58584148

void fixCRC(u8 *buff){
	u16 crc16=crc_16(buff+4, 0x410);
	memcpy(buff+2, &crc16, 2);
}

Result check_slots(){
	printf("%sSlot Status:\n", blue);
	for(int i=0; i<3; i++){
		printf("%d)", i+1);
		_CFG_GetConfigInfoBlk4(0xC00, 0x80000+i, workbuf);
		if(*(u32*)(workbuf+0x420) == HAXX){
			printf(" Haxx\n");
		}
		else{
			printf(" User\n");
		}
	}
	printf("%s\n", white);
	
	return 0;
}

Result restore_slots(){
	Result res;
	for(int i=0; i<3; i++){
		printf("Restoring slot %d... ", i+1);
		memset(workbuf, 0, 0xC00);
		_CFG_GetConfigInfoBlk4(0xC00, 0x80000+i, workbuf);
		if(*(u32*)(workbuf+0x420) == HAXX){
			memcpy(workbuf, workbuf+0x500, 0x500); //restore backup slot to wifi slot
			memset(workbuf+0x500, 0, 0x500);       //clear slot backup to zeros
			res = _CFG_SetConfigInfoBlk4(0xC00, 0x80000+i, workbuf); //commit workbuf to slot
		}
		else{
			res = 1;
		}
		if(res) printf(" FAIL\n");
		else    printf(" GOOD\n");
	}
	_CFG_UpdateConfigSavegame();
	
	return 0;
}

Result inject_slots(){
	Result res;
	for(int i=0; i<3; i++){
		printf("Injecting slot %d... ", i+1);
		memset(workbuf, 0, 0xC00);
		_CFG_GetConfigInfoBlk4(0xC00, 0x80000+i, workbuf);
		if(*(u32*)(workbuf+0x420) == HAXX){
			res = 1;
		}
		else{
			memcpy(workbuf+0x500, workbuf, 0x500); //backup user slot to slot+0x500
			memcpy(workbuf, slot1_bin, 0x500);         //write slot1 to workbuf
			res = _CFG_SetConfigInfoBlk4(0xC00, 0x80000+i, workbuf); //commit workbuf to slot
		}
		if(res) printf(" FAIL\n");
		else    printf(" GOOD\n");
	}
	_CFG_UpdateConfigSavegame();
	 
	 return 0;
}

bool crcFile(const char *filename, u32 crc){
	if(!fileExists(filename)) return false;
	u32 size=(u32)getFileSize(filename);
	u8 *buf=(u8*)malloc(0x400000);
	u32 filecrc=0;
	readFile(filename, buf, size);
	
	filecrc=crc32(buf, size);
	free(buf);
	
	if(crc != filecrc){
		printf("crc err - file:%08X expect:%08X\n", (int)filecrc, (int)crc);
		return false;
	}
	
	return true;
}

u32 dl_attempt, dl_success;
Result http_download(const char *url, const char *filename, u32 crc)
{
	dl_attempt++;
	Result ret=0;
	httpcContext context;
	char *newurl=NULL;
	u32 statuscode=0;
	u32 contentsize=0, readsize=0, size=0;
	u8 *buf, *lastbuf;

	printf("\nDownloading %s\n", filename);
	
	if(crcFile(filename, crc)==true){
		printf("Already downloaded\n\n");
		dl_success++;
		return 0;
	}

	do {
		ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 1);
		//printf("return from httpcOpenContext: %" PRId32 "\n",ret);

		// This disables SSL cert verification, so https:// will be usable
		ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
		//printf("return from httpcSetSSLOpt: %" PRId32 "\n",ret);

		// Enable Keep-Alive connections
		ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
		//printf("return from httpcSetKeepAlive: %" PRId32 "\n",ret);

		// Set a User-Agent header so websites can identify your application
		ret = httpcAddRequestHeaderField(&context, "User-Agent", "httpc-example/1.0.0");
		//printf("return from httpcAddRequestHeaderField: %" PRId32 "\n",ret);

		// Tell the server we can support Keep-Alive connections.
		// This will delay connection teardown momentarily (typically 5s)
		// in case there is another request made to the same server.
		ret = httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");
		//printf("return from httpcAddRequestHeaderField: %" PRId32 "\n",ret);

		ret = httpcBeginRequest(&context);
		if(ret!=0){
			httpcCloseContext(&context);
			if(newurl!=NULL) free(newurl);
			return ret;
		}

		ret = httpcGetResponseStatusCode(&context, &statuscode);
		if(ret!=0){
			httpcCloseContext(&context);
			if(newurl!=NULL) free(newurl);
			return ret;
		}

		if ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308)) {
			if(newurl==NULL) newurl = (char*)malloc(0x1000); // One 4K page for new URL
			if (newurl==NULL){
				httpcCloseContext(&context);
				return -1;
			}
			ret = httpcGetResponseHeader(&context, "Location", newurl, 0x1000);
			url = newurl; // Change pointer to the url that we just learned
			printf("redirecting to url: %s\n",url);
			httpcCloseContext(&context); // Close this context before we try the next
		}
	} while ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308));

	if(statuscode!=200){
		printf("URL returned status: %" PRId32 "\n", statuscode);
		httpcCloseContext(&context);
		if(newurl!=NULL) free(newurl);
		return -2;
	}

	// This relies on an optional Content-Length header and may be 0
	ret=httpcGetDownloadSizeState(&context, NULL, &contentsize);
	if(ret!=0){
		httpcCloseContext(&context);
		if(newurl!=NULL) free(newurl);
		return ret;
	}

	printf("reported size: %" PRId32 " ",contentsize);

	// Start with a single page buffer
	buf = (u8*)malloc(0x1000);
	if(buf==NULL){
		httpcCloseContext(&context);
		if(newurl!=NULL) free(newurl);
		return -1;
	}

	do {
		// This download loop resizes the buffer as data is read.
		ret = httpcDownloadData(&context, buf+size, 0x1000, &readsize);
		size += readsize; 
		if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING){
				lastbuf = buf; // Save the old pointer, in case realloc() fails.
				buf = (u8*)realloc(buf, size + 0x1000);
				if(buf==NULL){ 
					httpcCloseContext(&context);
					free(lastbuf);
					if(newurl!=NULL) free(newurl);
					return -1;
				}
			}
	} while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);	

	if(ret!=0){
		httpcCloseContext(&context);
		if(newurl!=NULL) free(newurl);
		free(buf);
		return -1;
	}

	// Resize the buffer back down to our actual final size
	lastbuf = buf;
	buf = (u8*)realloc(buf, size);
	if(buf==NULL){ // realloc() failed.
		httpcCloseContext(&context);
		free(lastbuf);
		if(newurl!=NULL) free(newurl);
		return -1;
	}

	printf("downloaded size: %" PRId32 "\n",size);
	
	if(size > 0x400000) size=0x400000;  //4MB sanity cap

	ret = writeFile(filename, buf, size);
	if(ret) return ret;

	httpcCloseContext(&context);
	free(buf);
	if (newurl!=NULL) free(newurl);

	dl_success++;
	return 0;
}


Result curl_download(const char *url, const char *filename, u32 crc)
{
	Result res;
	
	printf("\nDownloading %s\n", filename);
	
	if(strstr(url, "usmlist.bin")){ //we don't need crc checks if we're downloading list
		res = downloadToFile(url, filename);
		if(res) return 2;
		return 0;
	}
	
	dl_attempt++;  //we don't want this counted if it's a usmlist download, hence going after the usmlist check
	
	if(crcFile(filename, crc)==true){
		printf("Already downloaded\n\n");
		dl_success++;
		return 0;
	}

	res = downloadToFile(url, filename);
	if(res) { 
		printf("result: %08X\n", (int) res);
		return 1; 
	}
	
	res = crcFile(filename, crc);
	if(res==false) { 
		printf("result: %08X\n", (int) res);
		return 3; 
	}

	dl_success++;
	return 0;
}

Result getlist(){
	FILE *f=fopen("/usmlist.bin","rb");
	if(f){
		u32 bytesread=fread(usmlist, 1, 0x4200, f);
		fclose(f);
		if(bytesread != 0x4100) return 1;
		return 0;
	}
	return 2;
}

int cursor=0;
int menu(u32 n){
	//Result res;
	consoleClear();
	printf("usmTool v1.0 - zoogie\n\n");
	Result res;
	u8 region=1;
	u32 lumaconfig[8]={0x464e4f43, 0x00040002, 0, 0, 0, 0x00020100, 0x00040010, 0};
	char url[0x100]={0};
	char filepath[0x100]={0};
	
	check_slots();

	char *choices[]={
		"AUTOMATIC download setup files & install usm to\n  wifi slots (recommended)",
		"INSTALL   usm to wifi slots & shutdown",
		"RESTORE   original wifi slots",
		"EXIT      to menu"
	};
	
	int maxchoices=sizeof(choices)/4; //each array element is a 32 bit pointer so numElements is sizeof/4 (this is a bad practice but whatever).
	
	if(n & KEY_UP) cursor--;
	else if (n & KEY_DOWN) cursor++;
	if (cursor >= maxchoices) cursor=0;
	else if (cursor < 0) cursor=maxchoices-1;
	
	
	for(int i=0; i<maxchoices; i++){
		printf("%s%s%s\n", cursor==i ? yellow:white, choices[i], white);
	}
	
	printf("--------------------------------------------------");
	printf(" \n");
	
	if(n & KEY_A) {
		
		switch(cursor){
			case 0:
			dl_attempt=0; dl_success=0;

			curl_download("https://github.com/zoogie/usmlist/blob/main/usmlist.bin?raw=true", "/usmlist.bin", 0);
			res = getlist();

			if(res){
				printf("could not download usmlist.bin %d\n", (int)res);
				break;
			}
			
			for(int i=0; i<64; i++){
				if(usmlist[i*0x100] == 0 || usmlist[i*0x100 + 0xC0] == 0) break;
				snprintf(url, 0xC0, "%s", usmlist + (i*0x100));
				snprintf(filepath, 0x40, "%s", usmlist + (i*0x100 + 0xC0));
				curl_download(url, filepath, *(u32*)(usmlist + 0x4000 + (i*4)));
			}
			
			res = _CFGU_SecureInfoGetRegion(&region);
			if(res) printf("ohno: %08X\n", (int)res);
			else{
				if(region > 3) region+=2;  //strange nintendo region math
				lumaconfig[5] |= (region << 12);
				writeFile("/luma/config.bin", lumaconfig, 32);   //sets up hbmenu to launch with download play, saving the user a lot of time
			}
			
			printf("\n%d/%d downloads succeeded\n", (int)dl_success, (int)dl_attempt);
			
			if(dl_attempt == dl_success){                         //if all downloads succeed, app moves on to case 1 for usm install.
				printf("\nFiles downloaded, now installing usm\n");
				svcSleepThread(0xFFFFFFFF);
			}
			else{
				printf("Press B to return to menu\n");
				break;
			}
			
			case 1:
			inject_slots();
			printf("powering down now...");
			printf("\n");
			svcSleepThread(1000*1000*1000);
			PTMSYSM_ShutdownAsync(1000*1000*1000);
			while(1) svcSleepThread(100*1000*1000);
			break;
			
			case 2:
			restore_slots();
			printf("rebooting now...");
			printf("\n");
			svcSleepThread(1000*1000*1000);
			APT_HardwareResetAsync();
			while(1) svcSleepThread(100*1000*1000);
			break;
			
			case 3:
			//APT_HardwareResetAsync();
			return 1;
			break;
			
			default:;
		};
		svcSleepThread(1000*1000*1000);
	}
	
	return 0;
}

int main(int argc, char* argv[])
{
	gfxInitDefault();

	consoleInit(GFX_TOP, NULL);
	printf("usmTool v1.0 - zoogie\n");

	Result res;
	u32 fail=0;
	usmlist=(u8*)linearAlloc(0x5000);
	fsInit();
	mkdir("/3DS", 0777);
	mkdir("/cias", 0777);
	mkdir("/luma", 0777);
	mkdir("/luma/payloads", 0777);
	mkdir("/gm9", 0777);
	mkdir("/gm9/scripts", 0777);
	mkdir("/gm9/support", 0777);
	res = ptmSysmInit();
	res = nsInit();
	res = httpcInit(0);
	res = _cfguInit();
	printf("cfgInit: %08X\n", (int)res);
	res = _CFG_GetConfigInfoBlk4(0xC00, 0x80000, workbuf);
	printf("cfgTest: %08X\n\n", (int)res);
	
	
	
	
	if(res){
		printf("WHAT IS WRONG WITH THE ELF?\n");
		printf("A: I need cfg:s or cfg:i!\n\n");
		//printf("(hint: sd:/3ds/slotTool/slotTool.xml)\n\n");
		printf("Press any key to exit :(\n");
		fail=1;
	}
	else{
		menu(0);
	}

	while (aptMainLoop())
	{
		gspWaitForVBlank();
		gfxSwapBuffers();
		hidScanInput();
		u32 kDown = hidKeysDown();
		
		if(kDown & 0xfff){
			if(fail) break;
			res = menu(kDown);
			if(res) break;
		}
	}

	//free(usmlist);
	httpcExit();
	gfxExit();
	return 0;
}
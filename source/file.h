#pragma once
#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "file.h"





#include <curl/curl.h>
//#include "common.hpp"

Result openFile(Handle* fileHandle, const char * path, bool write);
Result deleteFile(const char * path);

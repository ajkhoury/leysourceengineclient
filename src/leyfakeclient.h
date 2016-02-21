#ifndef LEYFAKESERV_H
#define LEYFAKESERV_H

#ifdef _WIN32
#pragma once
#define lastneterror() WSAGetLastError()
#else
#define lastneterror() errno
#endif

#include "stdafx.h"





#include "buf.h"
#include "checksum_crc.h"
#include "leychan.h"

extern leychan *netchan;

#endif

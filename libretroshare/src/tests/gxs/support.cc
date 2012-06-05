/*
 * libretroshare/src/serialiser: t_support.h.cc
 *
 * RetroShare Serialiser tests.
 *
 * Copyright 2007-2008 by Christopher Evi-Parker
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License Version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA.
 *
 * Please report all bugs and problems to "retroshare@lunamutt.com".
 *
 */

#include <stdlib.h>

#include "support.h"
#include "serialiser/rstlvbase.h"

void randString(const uint32_t length, std::string& outStr)
{
	char alpha = 'a';
	char* stringData = NULL;

	stringData = new char[length];

	for(uint32_t i=0; i != length; i++)
		stringData[i] = alpha + (rand() % 26);

	outStr.assign(stringData, length);
	delete[] stringData;

	return;
}

void randString(const uint32_t length, std::wstring& outStr)
{
	wchar_t alpha = L'a';
	wchar_t* stringData = NULL;

	stringData = new wchar_t[length];

	for(uint32_t i=0; i != length; i++)
		stringData[i] = (alpha + (rand() % 26));

	outStr.assign(stringData, length);
	delete[] stringData;

	return;
}

bool operator==(const RsTlvSecurityKey& sk1, const RsTlvSecurityKey& sk2)
{

	if(sk1.startTS != sk2.startTS) return false;
	if(sk1.endTS != sk2.endTS) return false;
	if(sk1.keyFlags != sk2.keyFlags) return false;
	if(sk1.keyId != sk2.keyId) return false;
	if(!(sk1.keyData == sk1.keyData)) return false;

	return true;
}

bool operator==(const RsTlvKeySignature& ks1, const RsTlvKeySignature& ks2)
{

	if(ks1.keyId != ks2.keyId) return false;
	if(!(ks1.signData == ks2.signData)) return false;

	return true;
}

bool operator==(const RsTlvPeerIdSet& pids1, const RsTlvPeerIdSet& pids2)
{
	std::list<std::string>::const_iterator it1 = pids1.ids.begin(),
			it2 = pids2.ids.begin();


	for(; ((it1 != pids1.ids.end()) && (it2 != pids2.ids.end())); it1++, it2++)
	{
		if(*it1 != *it2) return false;
	}

	return true;

}


void init_item(RsTlvImage& im)
{
	std::string imageData;
	randString(LARGE_STR, imageData);
	im.binData.setBinData(imageData.c_str(), imageData.size());
	im.image_type = RSTLV_IMAGE_TYPE_PNG;

	return;
}

bool operator==(const RsTlvBinaryData& bd1, const RsTlvBinaryData& bd2)
{
	if(bd1.tlvtype != bd2.tlvtype) return false;
	if(bd1.bin_len != bd2.bin_len) return false;

	unsigned char *bin1 = (unsigned char*)(bd1.bin_data),
			*bin2 = (unsigned char*)(bd2.bin_data);

	for(uint32_t i=0; i < bd1.bin_len; bin1++, bin2++, i++)
	{
		if(*bin1 != *bin2)
			return false;
	}

	return true;
}


void init_item(RsTlvSecurityKey& sk)
{
	int randnum = rand()%313131;

	sk.endTS = randnum;
	sk.keyFlags = randnum;
	sk.startTS = randnum;
	randString(SHORT_STR, sk.keyId);

	std::string randomStr;
	randString(LARGE_STR, randomStr);

	sk.keyData.setBinData(randomStr.c_str(), randomStr.size());

	return;
}

void init_item(RsTlvKeySignature& ks)
{
	randString(SHORT_STR, ks.keyId);

	std::string signData;
	randString(LARGE_STR, signData);

	ks.signData.setBinData(signData.c_str(), signData.size());

	return;
}


bool operator==(const RsTlvImage& img1, const RsTlvImage& img2)
{
	if(img1.image_type != img2.image_type) return false;
	if(!(img1.binData == img2.binData)) return false;

	return true;

}

/** channels, forums and blogs **/

void init_item(RsTlvHashSet& hs)
{
	std::string hash;

	for(int i=0; i < 10; i++)
	{
		randString(SHORT_STR, hash);
		hs.ids.push_back(hash);
	}

	hs.mType = TLV_TYPE_HASHSET;
	return;
}

void init_item(RsTlvPeerIdSet& ps)
{
	std::string peer;

	for(int i=0; i < 10; i++)
	{
		randString(SHORT_STR, peer);
		ps.ids.push_back(peer);
	}

	ps.mType = TLV_TYPE_PEERSET;
	return;
}

bool operator==(const RsTlvHashSet& hs1,const RsTlvHashSet& hs2)
{
	if(hs1.mType != hs2.mType) return false;

	std::list<std::string>::const_iterator it1 = hs1.ids.begin(),
			it2 = hs2.ids.begin();

	for(; ((it1 != hs1.ids.end()) && (it2 != hs2.ids.end())); it1++, it2++)
	{
		if(*it1 != *it2) return false;
	}

	return true;
}

void init_item(RsTlvFileItem& fi)
{
	fi.age = rand()%200;
	fi.filesize = rand()%34030313;
	randString(SHORT_STR, fi.hash);
	randString(SHORT_STR, fi.name);
	randString(SHORT_STR, fi.path);
	fi.piecesize = rand()%232;
	fi.pop = rand()%2354;
	init_item(fi.hashset);

	return;
}

void init_item(RsTlvBinaryData& bd){
    bd.TlvClear();
    std::string data;
    randString(LARGE_STR, data);
    bd.setBinData(data.data(), data.length());
}

void init_item(RsTlvFileSet& fSet){

	randString(LARGE_STR, fSet.comment);
	randString(SHORT_STR, fSet.title);
	RsTlvFileItem fi1, fi2;
	init_item(fi1);
	init_item(fi2);
	fSet.items.push_back(fi1);
	fSet.items.push_back(fi2);

	return;
}

bool operator==(const RsTlvFileSet& fs1,const  RsTlvFileSet& fs2)
{
	if(fs1.comment != fs2.comment) return false;
	if(fs1.title != fs2.title) return false;

	std::list<RsTlvFileItem>::const_iterator it1 = fs1.items.begin(),
			it2 = fs2.items.begin();

	for(;  ((it1 != fs1.items.end()) && (it2 != fs2.items.end())); it1++, it2++)
		if(!(*it1 == *it2)) return false;

	return true;
}

bool operator==(const RsTlvFileItem& fi1,const RsTlvFileItem& fi2)
{
	if(fi1.age != fi2.age) return false;
	if(fi1.filesize != fi2.filesize) return false;
	if(fi1.hash != fi2.hash) return false;
	if(!(fi1.hashset == fi2.hashset)) return false;
	if(fi1.name != fi2.name) return false;
	if(fi1.path != fi2.path) return false;
	if(fi1.piecesize != fi2.piecesize) return false;
	if(fi1.pop != fi2.pop) return false;

	return true;
}

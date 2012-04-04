#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <openpgpsdk/util.h>
#include <openpgpsdk/crypto.h>
#include <openpgpsdk/armour.h>
#include <openpgpsdk/keyring.h>
#include <openpgpsdk/readerwriter.h>
#include <openpgpsdk/validate.h>
}
#include "pgphandler.h"

std::string	PGPIdType::toStdString() const
{
	static const char out[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' } ;

	std::string res ;

	for(int j = 0; j < KEY_ID_SIZE; j++)
	{
		res += out[ (bytes[j]>>4) ] ;
		res += out[ bytes[j] & 0xf ] ;
	}

	return res ;
}
std::string	PGPFingerprintType::toStdString() const
{
	static const char out[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' } ;

	std::string res ;

	for(int j = 0; j < KEY_FINGERPRINT_SIZE; j++)
	{
		res += out[ (bytes[j]>>4) ] ;
		res += out[ bytes[j] & 0xf ] ;
	}

	return res ;
}


PGPIdType PGPIdType::fromUserId_hex(const std::string& s)
{
	int n=0;
	if(s.length() != KEY_ID_SIZE*2)
		throw std::runtime_error("PGPIdType::PGPIdType: can only init from 16 chars hexadecimal string") ;

	PGPIdType res ;

	for(int i = 0; i < KEY_ID_SIZE; ++i)
	{
		res.bytes[i] = 0 ;

		for(int k=0;k<2;++k)
		{
			char b = s[n++] ;

			if(b >= 'A' && b <= 'F')
				res.bytes[i] += (b-'A'+10) << 4*(1-k) ;
			else if(b >= 'a' && b <= 'f')
				res.bytes[i] += (b-'a'+10) << 4*(1-k) ;
			else if(b >= '0' && b <= '9')
				res.bytes[i] += (b-'0') << 4*(1-k) ;
			else
				throw std::runtime_error("PGPIdType::Sha1CheckSum: can't init from non pure hexadecimal string") ;
		}
	}
	return res ;
}
PGPIdType PGPIdType::fromFingerprint_hex(const std::string& s)
{
	if(s.length() != PGPFingerprintType::KEY_FINGERPRINT_SIZE*2)
		throw std::runtime_error("PGPIdType::PGPIdType: can only init from 40 chars hexadecimal string") ;

	PGPIdType res ;

	int n=PGPFingerprintType::KEY_FINGERPRINT_SIZE - PGPIdType::KEY_ID_SIZE -1;

	for(int i = 0; i < PGPIdType::KEY_ID_SIZE; ++i)
	{
		res.bytes[i] = 0 ;

		for(int k=0;k<2;++k)
		{
			char b = s[n++] ;

			if(b >= 'A' && b <= 'F')
				res.bytes[i] += (b-'A'+10) << 4*(1-k) ;
			else if(b >= 'a' && b <= 'f')
				res.bytes[i] += (b-'a'+10) << 4*(1-k) ;
			else if(b >= '0' && b <= '9')
				res.bytes[i] += (b-'0') << 4*(1-k) ;
			else
				throw std::runtime_error("PGPIdType::Sha1CheckSum: can't init from non pure hexadecimal string") ;
		}
	}
	return res ;
}
PGPFingerprintType PGPFingerprintType::fromFingerprint_hex(const std::string& s)
{
	int n=0;
	if(s.length() != PGPFingerprintType::KEY_FINGERPRINT_SIZE*2)
		throw std::runtime_error("PGPIdType::PGPIdType: can only init from 40 chars hexadecimal string") ;

	PGPFingerprintType res ;

	for(int i = 0; i < PGPFingerprintType::KEY_FINGERPRINT_SIZE; ++i)
	{
		res.bytes[i] = 0 ;

		for(int k=0;k<2;++k)
		{
			char b = s[n++] ;

			if(b >= 'A' && b <= 'F')
				res.bytes[i] += (b-'A'+10) << 4*(1-k) ;
			else if(b >= 'a' && b <= 'f')
				res.bytes[i] += (b-'a'+10) << 4*(1-k) ;
			else if(b >= '0' && b <= '9')
				res.bytes[i] += (b-'0') << 4*(1-k) ;
			else
				throw std::runtime_error("PGPIdType::Sha1CheckSum: can't init from non pure hexadecimal string") ;
		}
	}
	return res ;
}
PGPIdType::PGPIdType(const unsigned char b[])
{
	memcpy(bytes,b,KEY_ID_SIZE) ;
}
PGPFingerprintType::PGPFingerprintType(const unsigned char b[])
{
	memcpy(bytes,b,KEY_FINGERPRINT_SIZE) ;
}


uint64_t PGPIdType::toUInt64() const
{
	uint64_t res = 0  ;

	for(int i=0;i<KEY_ID_SIZE;++i)
		res = (res << 8) + bytes[i] ;

	return res ;
}

ops_keyring_t *PGPHandler::allocateOPSKeyring() 
{
	ops_keyring_t *kr = (ops_keyring_t*)malloc(sizeof(ops_keyring_t)) ;
	kr->nkeys = 0 ;
	kr->nkeys_allocated = 0 ;
	kr->keys = 0 ;

	return kr ;
}

PGPHandler::PGPHandler(const std::string& pubring, const std::string& secring,PassphraseCallback cb)
	: pgphandlerMtx(std::string("PGPHandler")), _pubring_path(pubring),_secring_path(secring),_passphrase_callback(cb)
{
	// Allocate public and secret keyrings.
	// 
	_pubring = allocateOPSKeyring() ;
	_secring = allocateOPSKeyring() ;

	// Read public and secret keyrings from supplied files.
	//
	if(ops_false == ops_keyring_read_from_file(_pubring, false, pubring.c_str()))
		throw std::runtime_error("PGPHandler::readKeyRing(): cannot read pubring.") ;

	const ops_keydata_t *keydata ;
	int i=0 ;
	while( (keydata = ops_keyring_get_key_by_index(_pubring,i)) != NULL )
	{
		_public_keyring_map[ PGPIdType(keydata->key_id).toUInt64() ] = i ;
		++i ;
	}

	std::cerr << "Pubring read successfully." << std::endl;

	if(ops_false == ops_keyring_read_from_file(_secring, false, secring.c_str()))
		throw std::runtime_error("PGPHandler::readKeyRing(): cannot read secring.") ;

	i=0 ;
	while( (keydata = ops_keyring_get_key_by_index(_secring,i)) != NULL )
	{
		_secret_keyring_map[ PGPIdType(keydata->key_id).toUInt64() ] = i ;
		++i ;
	}

	std::cerr << "Secring read successfully." << std::endl;
}

PGPHandler::~PGPHandler()
{
	std::cerr << "Freeing PGPHandler. Deleting keyrings." << std::endl;

	// no need to free the the _map_ elements. They will be freed by the following calls:
	//
	ops_keyring_free(_pubring) ;
	ops_keyring_free(_secring) ;

	free(_pubring) ;
	free(_secring) ;
}

void PGPHandler::printKeys() const
{
	std::cerr << "Public keyring: " << std::endl;
	ops_keyring_list(_pubring) ;

	std::cerr << "Secret keyring: " << std::endl;
	ops_keyring_list(_secring) ;
}

bool PGPHandler::availableGPGCertificatesWithPrivateKeys(std::list<PGPIdType>& ids)
{
	// go through secret keyring, and check that we have the pubkey as well.
	//
	
	const ops_keydata_t *keydata = NULL ;
	int i=0 ;

	while( (keydata = ops_keyring_get_key_by_index(_secring,i++)) != NULL )
	{
		// check that the key is in the pubring as well

		if(ops_keyring_find_key_by_id(_pubring,keydata->key_id) != NULL)
			ids.push_back(PGPIdType(keydata->key_id)) ;
	}

	return true ;
}

// static ops_parse_cb_return_t cb_get_passphrase(const ops_parser_content_t *content_,ops_parse_cb_info_t *cbinfo __attribute__((unused)))
// {
// 	const ops_parser_content_union_t *content=&content_->content;
// 	//    validate_key_cb_arg_t *arg=ops_parse_cb_get_arg(cbinfo);
// 	//    ops_error_t **errors=ops_parse_cb_get_errors(cbinfo);
// 
// 	switch(content_->tag)
// 	{
// 		case OPS_PARSER_CMD_GET_SK_PASSPHRASE:
// 			/*
// 				Doing this so the test can be automated.
// 			 */
// 			*(content->secret_key_passphrase.passphrase)=ops_malloc_passphrase("hello");
// 			return OPS_KEEP_MEMORY;
// 			break;
// 
// 		default:
// 			break;
// 	}
// 
// 	return OPS_RELEASE_MEMORY;
// }

bool PGPHandler::GeneratePGPCertificate(const std::string& name, const std::string& email, const std::string& passwd, PGPIdType& pgpId, std::string& errString) 
{
	static const int KEY_NUMBITS = 2048 ;

	ops_user_id_t uid ;
	char *s = strdup((name + " " + email + " (Generated by RetroShare)").c_str()) ;
	uid.user_id = (unsigned char *)s ;
	unsigned long int e = 17 ; // some prime number

	ops_keydata_t *key = ops_rsa_create_selfsigned_keypair(KEY_NUMBITS,e,&uid) ;

	free(s) ;

	if(!key)
		return false ;

	// 1 - get a passphrase for encrypting.
	
	std::string passphrase = _passphrase_callback("Please enter passwd for encrypting your key : ") ;

	// 2 - save the private key encrypted to a temporary memory buffer

	ops_create_info_t *cinfo = NULL ;
	ops_memory_t *buf = NULL ;
   ops_setup_memory_write(&cinfo, &buf, 0);

	ops_write_transferable_secret_key(key,(unsigned char *)passphrase.c_str(),passphrase.length(),ops_false,cinfo);

	ops_keydata_free(key) ;

	// 3 - read the file into a keyring
	
	ops_keyring_t *tmp_keyring = allocateOPSKeyring() ;
	if(! ops_keyring_read_from_mem(tmp_keyring, ops_false, buf))
	{
		std::cerr << "Cannot re-read key from memory!!" << std::endl;
		return false ;
	}
	ops_teardown_memory_write(cinfo,buf);	// cleanup memory

	// 4 - copy the private key to the private keyring
	
	pgpId = PGPIdType(tmp_keyring->keys[0].key_id) ;
	addNewKeyToOPSKeyring(_secring,tmp_keyring->keys[0]) ;
	_secret_keyring_map[ pgpId.toUInt64() ] = _secring->nkeys-1 ;

	std::cerr << "Added new secret key with id " << pgpId.toStdString() << " to secret keyring." << std::endl;

	// 5 - copy the private key to the public keyring
	
	addNewKeyToOPSKeyring(_pubring,tmp_keyring->keys[0]) ;
	_public_keyring_map[ pgpId.toUInt64() ] = _pubring->nkeys-1 ;

	std::cerr << "Added new public key with id " << pgpId.toStdString() << " to public keyring." << std::endl;

	// 6 - clean

	ops_keyring_free(tmp_keyring) ;
	free(tmp_keyring) ;
	
	return true ;
}

std::string PGPHandler::makeRadixEncodedPGPKey(const ops_keydata_t *key)
{
	ops_boolean_t armoured=ops_true;
   ops_create_info_t* cinfo;

	ops_memory_t *buf = NULL ;
   ops_setup_memory_write(&cinfo, &buf, 0);

   ops_write_transferable_public_key(key,armoured,cinfo);
	ops_writer_close(cinfo) ;

	std::string akey((char *)ops_memory_get_data(buf),ops_memory_get_length(buf)) ;

   ops_teardown_memory_write(cinfo,buf);

	return akey ;
}

const ops_keydata_t *PGPHandler::getSecretKey(const PGPIdType& id) const
{
	std::map<uint64_t,uint32_t>::const_iterator res = _secret_keyring_map.find(id.toUInt64()) ;

	if(res == _secret_keyring_map.end())
		return NULL ;
	else
		return ops_keyring_get_key_by_index(_secring,res->second) ;
}
const ops_keydata_t *PGPHandler::getPublicKey(const PGPIdType& id) const
{
	std::map<uint64_t,uint32_t>::const_iterator res = _public_keyring_map.find(id.toUInt64()) ;

	if(res == _public_keyring_map.end())
		return NULL ;
	else
		return ops_keyring_get_key_by_index(_pubring,res->second) ;
}

std::string PGPHandler::SaveCertificateToString(const PGPIdType& id,bool include_signatures)
{
	const ops_keydata_t *key = getPublicKey(id) ;

	if(key == NULL)
	{
		std::cerr << "Cannot output key " << id.toStdString() << ": not found in keyring." << std::endl;
		return "" ;
	}

	return makeRadixEncodedPGPKey(key) ;
}

void PGPHandler::addNewKeyToOPSKeyring(ops_keyring_t *kr,const ops_keydata_t& key)
{
	kr->keys = (ops_keydata_t*)realloc(kr->keys,(kr->nkeys+1)*sizeof(ops_keydata_t)) ;
	memset(&kr->keys[kr->nkeys],0,sizeof(ops_keydata_t)) ;
	ops_keydata_copy(&kr->keys[kr->nkeys],&key) ;
	kr->nkeys++ ;
}

bool PGPHandler::LoadCertificateFromString(const std::string& pgp_cert,PGPIdType& id,std::string& error_string)
{
	ops_keyring_t *tmp_keyring = allocateOPSKeyring();
	ops_memory_t *mem = ops_memory_new() ;
	ops_memory_add(mem,(unsigned char *)pgp_cert.c_str(),pgp_cert.length()) ;

	if(!ops_keyring_read_from_mem(tmp_keyring,ops_true,mem))
	{
		ops_keyring_free(tmp_keyring) ;
		free(tmp_keyring) ;
		ops_memory_release(mem) ;
		free(mem) ;

		std::cerr << "Could not read key. Format error?" << std::endl;
		error_string = std::string("Could not read key. Format error?") ;
		return false ;
	}
	ops_memory_release(mem) ;
	free(mem) ;
	error_string.clear() ;

	std::cerr << "Key read correctly: " << std::endl;
	ops_keyring_list(tmp_keyring) ;

	const ops_keydata_t *keydata = NULL ;
	int i=0 ;

	while( (keydata = ops_keyring_get_key_by_index(tmp_keyring,i++)) != NULL )
	{
		id = PGPIdType(keydata->key_id) ;

		addNewKeyToOPSKeyring(_pubring,*keydata) ;
		_public_keyring_map[id.toUInt64()] = _pubring->nkeys-1 ;
	}

	std::cerr << "Added the key in the main public keyring." << std::endl;
	
	ops_keyring_free(tmp_keyring) ;
	free(tmp_keyring) ;

	return true ;
}

bool PGPHandler::SignDataBin(const PGPIdType& id,const void *data, const uint32_t len, unsigned char *sign, unsigned int *signlen)
{
	// need to find the key and to decrypt it.
	
	const ops_keydata_t *key = getSecretKey(id) ;

	if(!key)
	{
		std::cerr << "Cannot sign: no secret key with id " << id.toStdString() << std::endl;
		return false ;
	}

	std::string passphrase = _passphrase_callback("Please enter passwd:") ;

	ops_secret_key_t *secret_key = ops_decrypt_secret_key_from_data(key,passphrase.c_str()) ;

	if(!secret_key)
	{
		std::cerr << "Key decryption went wrong. Wrong passwd?" << std::endl;
		return false ;
	}

	// then do the signature.

	ops_memory_t *memres = ops_sign_buf(data,len,(ops_sig_type_t)0x10,secret_key,ops_false) ;

	if(!memres)
		return false ;

	uint32_t tlen = std::min(*signlen,(uint32_t)ops_memory_get_length(memres)) ;

	memcpy(sign,ops_memory_get_data(memres),tlen) ;
	*signlen = tlen ;

	ops_memory_release(memres) ;
	free(memres) ;
	ops_secret_key_free(secret_key) ;
	free(secret_key) ;

	return true ;
}

bool PGPHandler::getKeyFingerprint(const PGPIdType& id,PGPFingerprintType& fp) const
{
	const ops_keydata_t *key = getPublicKey(id) ;

	if(key == NULL)
		return false ;

	ops_fingerprint_t f ;
	ops_fingerprint(&f,&key->key.pkey) ; 

	fp = PGPFingerprintType(f.fingerprint) ;

	return true ;
}

bool PGPHandler::VerifySignBin(const void *data, uint32_t data_len, unsigned char *sign, unsigned int sign_len, const PGPFingerprintType& key_fingerprint)
{
	PGPIdType id = PGPIdType::fromFingerprint_hex(key_fingerprint.toStdString()) ;
	const ops_keydata_t *key = getPublicKey(id) ;

	if(key == NULL)
	{
		std::cerr << "No key returned by fingerprint " << key_fingerprint.toStdString() << ", and ID " << id.toStdString() << ", signature verification failed!" << std::endl;
		return false ;
	}

	// Check that fingerprint is the same.
	const ops_public_key_t *pkey = &key->key.pkey ;
	ops_fingerprint_t fp ;
	ops_fingerprint(&fp,pkey) ; 

	if(key_fingerprint != PGPFingerprintType(fp.fingerprint))
	{
		std::cerr << "Key fingerprint does not match " << key_fingerprint.toStdString() << ", for ID " << id.toStdString() << ", signature verification failed!" << std::endl;
		return false ;
	}

	ops_signature_t signature ;
//	ops_signature_add_data(&signature,sign,sign_len) ;

//	ops_boolean_t valid=check_binary_signature(data_len,data,signature,pkey) ;

	return false ;
}


/*
 * libretroshare/src/pqi: p3netmgr.cc
 *
 * 3P/PQI network interface for RetroShare.
 *
 * Copyright 2007-2011 by Robert Fernie.
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

#include "pqi/p3netmgr.h"

#include "pqi/p3peermgr.h"
#include "pqi/p3linkmgr.h"

#include "util/rsnet.h"
#include "util/rsrandom.h"

#include "util/extaddrfinder.h"
#include "util/dnsresolver.h"

//#include "util/rsprint.h"
//#include "util/rsdebug.h"
//const int p3connectzone = 3431;

#include "serialiser/rsconfigitems.h"
#include "pqi/pqinotify.h"
#include "retroshare/rsiface.h"

#include <sstream>

/* Network setup States */

const uint32_t RS_NET_NEEDS_RESET = 	0x0000;
const uint32_t RS_NET_UNKNOWN = 	0x0001;
const uint32_t RS_NET_UPNP_INIT = 	0x0002;
const uint32_t RS_NET_UPNP_SETUP =  	0x0003;
const uint32_t RS_NET_EXT_SETUP =  	0x0004;
const uint32_t RS_NET_DONE =    	0x0005;
const uint32_t RS_NET_LOOPBACK =    	0x0006;
const uint32_t RS_NET_DOWN =    	0x0007;

/* Stun modes (TODO) */
const uint32_t RS_STUN_DHT =      	0x0001;
const uint32_t RS_STUN_DONE =      	0x0002;
const uint32_t RS_STUN_LIST_MIN =      	100;
const uint32_t RS_STUN_FOUND_MIN =     	10;

const uint32_t MAX_UPNP_INIT = 		60; /* seconds UPnP timeout */
const uint32_t MAX_UPNP_COMPLETE = 	600; /* 10 min... seems to take a while */
const uint32_t MAX_NETWORK_INIT =	70; /* timeout before network reset */

const uint32_t MIN_TIME_BETWEEN_NET_RESET = 		5;

/****
 * #define NETMGR_DEBUG 1
 * #define NETMGR_DEBUG_RESET 1
 * #define NETMGR_DEBUG_TICK 1
 ***/

#define NETMGR_DEBUG 1
#define NETMGR_DEBUG_RESET 1

pqiNetStatus::pqiNetStatus()
	:mLocalAddrOk(false), mExtAddrOk(false), mExtAddrStableOk(false), 
	mUpnpOk(false), mDhtOk(false), mResetReq(false)
{
        mDhtNetworkSize = 0;
        mDhtRsNetworkSize = 0;

	sockaddr_clear(&mLocalAddr);
	sockaddr_clear(&mExtAddr);
	return;
}



void pqiNetStatus::print(std::ostream &out)
{
	out << "pqiNetStatus: ";
	out << "mLocalAddrOk: " << mLocalAddrOk; 
        out << " mExtAddrOk: " << mExtAddrOk;
        out << " mExtAddrStableOk: " << mExtAddrStableOk;
	out << std::endl;
        out << " mUpnpOk: " << mUpnpOk;
        out << " mDhtOk: " << mDhtOk;
        out << " mResetReq: " << mResetReq;
        out << std::endl;
	out << "mDhtNetworkSize: " << mDhtNetworkSize << " mDhtRsNetworkSize: " << mDhtRsNetworkSize;
        out << std::endl;
	out << "mLocalAddr: " << rs_inet_ntoa(mLocalAddr.sin_addr) << ":" << ntohs(mLocalAddr.sin_port) << " ";
	out << "mExtAddr: " << rs_inet_ntoa(mExtAddr.sin_addr) << ":" << ntohs(mExtAddr.sin_port) << " ";
	out << " NetOk: " << NetOk();
        out << std::endl;
}


p3NetMgr::p3NetMgr()
	:mPeerMgr(NULL), mLinkMgr(NULL), mNetMtx("p3NetMgr"),
	mNetStatus(RS_NET_UNKNOWN), mStatusChanged(false)
{

	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

		mNetMode = RS_NET_MODE_UDP;

		mUseExtAddrFinder = true;
		mExtAddrFinder = new ExtAddrFinder();
		mNetInitTS = 0;
	
		mNetFlags = pqiNetStatus();
		mOldNetFlags = pqiNetStatus();

	}
	
#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr() Startup" << std::endl;
#endif

	netReset();

	return;
}

void p3NetMgr::setManagers(p3PeerMgr *peerMgr, p3LinkMgr *linkMgr)
{
	mPeerMgr = peerMgr;
	mLinkMgr = linkMgr;
}

//void p3NetMgr::setDhtMgr(p3DhtMgr *dhtMgr)
//{
//	mDhtMgr = dhtMgr;
//}

void p3NetMgr::setAddrAssist(pqiAddrAssist *dhtStun, pqiAddrAssist *proxyStun)
{
	mDhtStunner = dhtStun;
	mProxyStunner = proxyStun;
}


uint32_t p3NetMgr::getNetStateMode()
{
	return 0;
}

uint32_t p3NetMgr::getNetworkMode()
{
	return 0;
}

uint32_t p3NetMgr::getNatTypeMode()
{
	return 0;
}

uint32_t p3NetMgr::getNatHoleMode()
{
	return 0;
}

uint32_t p3NetMgr::getConnectModes()
{
	return 0;
}



/***** Framework / initial implementation for a connection manager.
 *
 * This needs a state machine for Initialisation.
 *
 * Network state:
 *   RS_NET_UNKNOWN
 *   RS_NET_EXT_UNKNOWN * forwarded port (but Unknown Ext IP) *
 *   RS_NET_EXT_KNOWN   * forwarded port with known IP/Port. *
 *
 *   RS_NET_UPNP_CHECK  * checking for UPnP *
 *   RS_NET_UPNP_KNOWN  * confirmed UPnP ext Ip/port *
 *
 *   RS_NET_UDP_UNKNOWN * not Ext/UPnP - to determine Ext IP/Port *
 *   RS_NET_UDP_KNOWN   * have Stunned for Ext Addr *
 *
 *  Transitions:
 *
 *  RS_NET_UNKNOWN -(config)-> RS_NET_EXT_UNKNOWN 
 *  RS_NET_UNKNOWN -(config)-> RS_NET_UPNP_UNKNOWN  
 *  RS_NET_UNKNOWN -(config)-> RS_NET_UDP_UNKNOWN
 *              
 *  RS_NET_EXT_UNKNOWN -(DHT(ip)/Stun)-> RS_NET_EXT_KNOWN
 *
 *  RS_NET_UPNP_UNKNOWN -(Upnp)-> RS_NET_UPNP_KNOWN
 *  RS_NET_UPNP_UNKNOWN -(timout/Upnp)-> RS_NET_UDP_UNKNOWN
 *
 *  RS_NET_UDP_UNKNOWN -(stun)-> RS_NET_UDP_KNOWN
 *
 *
 * STUN state:
 * 	RS_STUN_INIT * done nothing *
 * 	RS_STUN_DHT  * looking up peers *
 * 	RS_STUN_DONE * found active peer and stunned *
 *
 *
 * Steps.
 *******************************************************************
 * (1) Startup.
 * 	- UDP port setup.
 * 	- DHT setup.
 * 	- Get Stun Keys -> add to DHT.
 *	- Feedback from DHT -> ask UDP to stun.
 *
 * (1) determine Network mode.
 *	If external Port.... Done: 
 * (2) 
 *******************************************************************
 * Stable operation:
 * (1) tick and check peers.
 * (2) handle callback.
 * (3) notify of new/failed connections.
 *
 *
 */

/* Called to reseet the whole network stack. this call is 
 * triggered by udp stun address tracking.
 *
 * must:
 * 	- reset UPnP and DHT.
 * 	- 
 */

void p3NetMgr::netReset()
{
#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgr::netReset() Called" << std::endl;
#endif

	shutdown(); /* blocking shutdown call */

	// Will initiate a new call for determining the external ip.
	if (mUseExtAddrFinder)
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgr::netReset() restarting AddrFinder" << std::endl;
#endif
		mExtAddrFinder->reset() ;
	}
	else
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgr::netReset() ExtAddrFinder Disabled" << std::endl;
#endif
	}

#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgr::netReset() resetting NetStatus" << std::endl;
#endif

	/* reset tcp network - if necessary */
	{
		/* NOTE: nNetListeners should be protected via the Mutex.
		* HOWEVER, as we NEVER change this list - once its setup
		* we can get away without it - and assume its constant.
		* 
		* NB: (*it)->reset_listener must be out of the mutex, 
		*      as it calls back to p3ConnMgr.
		*/

		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

		struct sockaddr_in iaddr = mLocalAddr;
		
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgr::netReset() resetting listeners" << std::endl;
#endif
		std::list<pqiNetListener *>::const_iterator it;
		for(it = mNetListeners.begin(); it != mNetListeners.end(); it++)
		{
			(*it)->resetListener(iaddr);
#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgr::netReset() reset listener" << std::endl;
#endif
		}
	}

	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
        	mNetStatus = RS_NET_UNKNOWN;
		netStatusReset_locked();
	}

#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgr::netReset() done" << std::endl;
#endif
}


void p3NetMgr::netStatusReset_locked()
{
	//std::cerr << "p3NetMgr::netStatusReset()" << std::endl;;

	mNetFlags = pqiNetStatus();
}


bool p3NetMgr::shutdown() /* blocking shutdown call */
{
#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr::shutdown()";
	std::cerr << std::endl;
#endif
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		mNetStatus = RS_NET_UNKNOWN;
		mNetInitTS = time(NULL);
		netStatusReset_locked();
	}
	netAssistFirewallShutdown();
	netAssistConnectShutdown();

	return true;
}









void p3NetMgr::netStartup()
{
	/* startup stuff */

	/* StunInit gets a list of peers, and asks the DHT to find them...
	 * This is needed for all systems so startup straight away 
	 */
#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgr::netStartup()" << std::endl;
#endif

        netDhtInit(); 

	/* decide which net setup mode we're going into 
	 */


	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

	mNetInitTS = time(NULL);
	netStatusReset_locked();

#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgr::netStartup() resetting mNetInitTS / Status" << std::endl;
#endif
	mNetMode &= ~(RS_NET_MODE_ACTUAL);

	switch(mNetMode & RS_NET_MODE_TRYMODE)
	{

		case RS_NET_MODE_TRY_EXT:  /* v similar to UDP */
#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgr::netStartup() TRY_EXT mode";
			std::cerr << std::endl;
#endif
			mNetMode |= RS_NET_MODE_EXT;
			mNetStatus = RS_NET_EXT_SETUP;
			break;

		case RS_NET_MODE_TRY_UDP:
#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgr::netStartup() TRY_UDP mode";
			std::cerr << std::endl;
#endif
			mNetMode |= RS_NET_MODE_UDP;
			mNetStatus = RS_NET_EXT_SETUP;
			break;

		default: // Fall through.

#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgr::netStartup() UNKNOWN mode";
			std::cerr << std::endl;
#endif

		case RS_NET_MODE_TRY_UPNP:
#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgr::netStartup() TRY_UPNP mode";
			std::cerr << std::endl;
#endif
			/* Force it here (could be default!) */
			mNetMode |= RS_NET_MODE_TRY_UPNP;
			mNetMode |= RS_NET_MODE_UDP;      /* set to UDP, upgraded is UPnP is Okay */
			mNetStatus = RS_NET_UPNP_INIT;
			break;
	}
}


void p3NetMgr::tick()
{
	netTick();
	netAssistConnectTick();
}

#define STARTUP_DELAY 5


void p3NetMgr::netTick()
{

#ifdef NETMGR_DEBUG_TICK
	std::cerr << "p3NetMgr::netTick()" << std::endl;
#endif

	// Check whether we are stuck on loopback. This happens if RS starts when
	// the computer is not yet connected to the internet. In such a case we
	// periodically check for a local net address.
	//
	checkNetAddress() ;

	uint32_t netStatus = 0;
	time_t   age = 0;
	{
		RsStackMutex stack(mNetMtx);   /************** LOCK MUTEX ***************/

		netStatus = mNetStatus;
		age = time(NULL) - mNetInitTS;

	}

	switch(netStatus)
	{
		case RS_NET_NEEDS_RESET:

#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netTick() STATUS: NEEDS_RESET" << std::endl;
#endif
			netReset();
			break;

		case RS_NET_UNKNOWN:
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netTick() STATUS: UNKNOWN" << std::endl;
#endif

			/* add a small delay to stop restarting straight after a RESET 
			 * This is so can we shutdown cleanly
			 */
			if (age < STARTUP_DELAY)
			{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
				std::cerr << "p3NetMgr::netTick() Delaying Startup" << std::endl;
#endif
			}
			else
			{
				netStartup();
			}

			break;

		case RS_NET_UPNP_INIT:
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netTick() STATUS: UPNP_INIT" << std::endl;
#endif
			netUpnpInit();
			break;

		case RS_NET_UPNP_SETUP:
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netTick() STATUS: UPNP_SETUP" << std::endl;
#endif
			netUpnpCheck();
			break;


		case RS_NET_EXT_SETUP:
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netTick() STATUS: EXT_SETUP" << std::endl;
#endif
			netExtCheck();
			break;

		case RS_NET_DONE:
#ifdef NETMGR_DEBUG_TICK
			std::cerr << "p3NetMgr::netTick() STATUS: DONE" << std::endl;
#endif

			break;

		case RS_NET_LOOPBACK:
                        //don't do a shutdown because a client in a computer without local network might be usefull for debug.
                        //shutdown();
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
                        std::cerr << "p3NetMgr::netTick() STATUS: RS_NET_LOOPBACK" << std::endl;
#endif
		default:
			break;
	}

	return;
}


void p3NetMgr::netDhtInit()
{
#if defined(NETMGR_DEBUG_RESET)
	std::cerr << "p3NetMgr::netDhtInit()" << std::endl;
#endif
	
	uint32_t vs = 0;
	{
		RsStackMutex stack(mNetMtx); /*********** LOCKED MUTEX ************/
		vs = mVisState;
	}
	
	enableNetAssistConnect(!(vs & RS_VIS_STATE_NODHT));
}


void p3NetMgr::netUpnpInit()
{
#if defined(NETMGR_DEBUG_RESET)
	std::cerr << "p3NetMgr::netUpnpInit()" << std::endl;
#endif
	uint16_t eport, iport;

	mNetMtx.lock();   /*   LOCK MUTEX */

	/* get the ports from the configuration */

	mNetStatus = RS_NET_UPNP_SETUP;
	iport = ntohs(mLocalAddr.sin_port);
	eport = ntohs(mExtAddr.sin_port);
	if ((eport < 1000) || (eport > 30000))
	{
		eport = iport;
	}

	mNetMtx.unlock(); /* UNLOCK MUTEX */

	netAssistFirewallPorts(iport, eport);
	enableNetAssistFirewall(true);
}

void p3NetMgr::netUpnpCheck()
{
	/* grab timestamp */
	mNetMtx.lock();   /*   LOCK MUTEX */

	time_t delta = time(NULL) - mNetInitTS;

#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
		std::cerr << "p3NetMgr::netUpnpCheck() age: " << delta << std::endl;
#endif

	mNetMtx.unlock(); /* UNLOCK MUTEX */

	struct sockaddr_in extAddr;
	int upnpState = netAssistFirewallActive();

	if (((upnpState == 0) && (delta > (time_t)MAX_UPNP_INIT)) ||
	    ((upnpState > 0) && (delta > (time_t)MAX_UPNP_COMPLETE)))
	{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
		std::cerr << "p3NetMgr::netUpnpCheck() ";
		std::cerr << "Upnp Check failed." << std::endl;
#endif
		/* fallback to UDP startup */
		mNetMtx.lock();   /*   LOCK MUTEX */

		/* UPnP Failed us! */
		mNetStatus = RS_NET_EXT_SETUP;
		mNetFlags.mUpnpOk = false;

		mNetMtx.unlock(); /* UNLOCK MUTEX */
	}
	else if ((upnpState > 0) && netAssistExtAddress(extAddr))
	{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
		std::cerr << "p3NetMgr::netUpnpCheck() ";
		std::cerr << "Upnp Check success state: " << upnpState << std::endl;
#endif
		/* switch to UDP startup */
		mNetMtx.lock();   /*   LOCK MUTEX */

		/* Set Net Status flags ....
		 * we now have external upnp address. Golden!
		 * don't set netOk flag until have seen some traffic.
		 */
		if (isValidNet(&(extAddr.sin_addr)))
		{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netUpnpCheck() ";
			std::cerr << "UpnpAddr: " << rs_inet_ntoa(extAddr.sin_addr);
			std::cerr << ":" << ntohs(extAddr.sin_port);
			std::cerr << std::endl;
#endif
			mNetFlags.mUpnpOk = true;
			mNetFlags.mExtAddr = extAddr;
			mNetFlags.mExtAddrOk = true;
			mNetFlags.mExtAddrStableOk = true;

			mNetStatus = RS_NET_EXT_SETUP;
			/* Fix netMode & Clear others! */
			mNetMode = RS_NET_MODE_TRY_UPNP | RS_NET_MODE_UPNP; 
		}
		mNetMtx.unlock(); /* UNLOCK MUTEX */
	}
	else
	{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
		std::cerr << "p3NetMgr::netUpnpCheck() ";
		std::cerr << "Upnp Check Continues: status: " << upnpState << std::endl;
#endif
	}

}


void p3NetMgr::netExtCheck()
{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
	std::cerr << "p3NetMgr::netExtCheck()" << std::endl;
#endif
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		bool isStable = false;
		struct sockaddr_in tmpip ;

			/* check for External Address */
		/* in order of importance */
		/* (1) UPnP -> which handles itself */
		if (!mNetFlags.mExtAddrOk)
		{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netExtCheck() Ext Not Ok" << std::endl;
#endif

			/* net Assist */
			if (netAssistExtAddress(tmpip))
			{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
				std::cerr << "p3NetMgr::netExtCheck() Ext supplied from netAssistExternalAddress()" << std::endl;
#endif
				if (isValidNet(&(tmpip.sin_addr)))
				{
					// must be stable???
					isStable = true;
					mNetFlags.mExtAddr = tmpip;
					mNetFlags.mExtAddrOk = true;
					mNetFlags.mExtAddrStableOk = isStable;
				}	
				else
				{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
					std::cerr << "p3NetMgr::netExtCheck() Bad Address supplied from netAssistExternalAddress()" << std::endl;
#endif
				}
			}

		}

		/* otherwise ask ExtAddrFinder */
		if (!mNetFlags.mExtAddrOk)
		{
			/* ExtAddrFinder */
			if (mUseExtAddrFinder)
			{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
				std::cerr << "p3NetMgr::netExtCheck() checking ExtAddrFinder" << std::endl;
#endif
				bool extFinderOk = mExtAddrFinder->hasValidIP(&(tmpip.sin_addr));
				if (extFinderOk)
				{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
					std::cerr << "p3NetMgr::netExtCheck() Ext supplied by ExtAddrFinder" << std::endl;
#endif
					/* best guess at port */
					tmpip.sin_port = mNetFlags.mLocalAddr.sin_port;
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
					std::cerr << "p3NetMgr::netExtCheck() ";
					std::cerr << "ExtAddr: " << rs_inet_ntoa(tmpip.sin_addr);
					std::cerr << ":" << ntohs(tmpip.sin_port);
					std::cerr << std::endl;
#endif

					mNetFlags.mExtAddr = tmpip;
					mNetFlags.mExtAddrOk = true;
					mNetFlags.mExtAddrStableOk = isStable;

					/* XXX HACK TO FIX */
#warning "ALLOWING ExtAddrFinder -> ExtAddrStableOk = true (which it is not normally)"
					mNetFlags.mExtAddrStableOk = true;

				}
			}
		}
				
		/* any other sources ??? */
		
		/* finalise address */
		if (mNetFlags.mExtAddrOk)
		{

#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netExtCheck() ";
			std::cerr << "ExtAddr: " << rs_inet_ntoa(mNetFlags.mExtAddr.sin_addr);
			std::cerr << ":" << ntohs(mNetFlags.mExtAddr.sin_port);
			std::cerr << std::endl;
#endif
			//update ip address list
			mExtAddr = mNetFlags.mExtAddr;

			mNetStatus = RS_NET_DONE;
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netExtCheck() Ext Ok: RS_NET_DONE" << std::endl;
#endif

			if (!mNetFlags.mExtAddrStableOk)
			{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
				std::cerr << "p3NetMgr::netUdpCheck() UDP Unstable :( ";
				std::cerr <<  std::endl;
				std::cerr << "p3NetMgr::netUdpCheck() We are unreachable";
				std::cerr <<  std::endl;
				std::cerr << "netMode =>  RS_NET_MODE_UNREACHABLE";
				std::cerr <<  std::endl;
#endif
				mNetMode &= ~(RS_NET_MODE_ACTUAL);
				mNetMode |= RS_NET_MODE_UNREACHABLE;

				/* send a system warning message */
				pqiNotify *notify = getPqiNotify();
				if (notify)
				{
					std::string title = 
						"Warning: Bad Firewall Configuration";

					std::string msg;
					msg +=  "               **** WARNING ****     \n";
					msg +=  "Retroshare has detected that you are behind";
					msg +=  " a restrictive Firewall\n";
					msg +=  "\n";
					msg +=  "You cannot connect to other firewalled peers\n";
					msg +=  "\n";
					msg +=  "You can fix this by:\n";
					msg +=  "   (1) opening an External Port\n";
					msg +=  "   (2) enabling UPnP, or\n";
					msg +=  "   (3) get a new (approved) Firewall/Router\n";

					notify->AddSysMessage(0, RS_SYS_WARNING, title, msg);
				}

			}
		}

		if (mNetFlags.mExtAddrOk)
		{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netExtCheck() setting netAssistSetAddress()" << std::endl;
#endif
			netAssistSetAddress(mNetFlags.mLocalAddr, mNetFlags.mExtAddr, mNetMode);
		}
#if 0
		else
		{
			std::cerr << "p3NetMgr::netExtCheck() setting ERR netAssistSetAddress(0)" << std::endl;
			/* mode = 0 for error */
			netAssistSetAddress(mNetFlags.mLocalAddr, mNetFlags.mExtAddr, mNetMode);
		}
#endif

		/* flag unreachables! */
		if ((mNetFlags.mExtAddrOk) && (!mNetFlags.mExtAddrStableOk))
		{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgr::netExtCheck() Ext Unstable - Unreachable Check" << std::endl;
#endif
		}
	}
}

/**********************************************************************************************
 ************************************** Interfaces    *****************************************
 **********************************************************************************************/

bool 	p3NetMgr::checkNetAddress()
{
	bool addrChanged = false;
	bool validAddr = false;
	
	struct in_addr prefAddr;
	struct sockaddr_in oldAddr;

	validAddr = getPreferredInterface(prefAddr);

	/* if we don't have a valid address - reset */
	if (!validAddr)
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgr::checkNetAddress() no Valid Network Address, resetting network." << std::endl;
		std::cerr << std::endl;
#endif
		netReset();
		return false;
	}
	
	
	/* check addresses */
	
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		
		oldAddr = mLocalAddr;
		addrChanged = (prefAddr.s_addr != mLocalAddr.sin_addr.s_addr);

#ifdef NETMGR_DEBUG_TICK
		std::cerr << "p3NetMgr::checkNetAddress()";
		std::cerr << std::endl;
		std::cerr << "Current Local: " << rs_inet_ntoa(mLocalAddr.sin_addr);
		std::cerr << ":" << ntohs(mLocalAddr.sin_port);
		std::cerr << std::endl;
		std::cerr << "Current Preferred: " << rs_inet_ntoa(prefAddr);
		std::cerr << std::endl;
#endif
		
#ifdef NETMGR_DEBUG_RESET
		if (addrChanged)
		{
			std::cerr << "p3NetMgr::checkNetAddress() Address Changed!";
			std::cerr << std::endl;
			std::cerr << "Current Local: " << rs_inet_ntoa(mLocalAddr.sin_addr);
			std::cerr << ":" << ntohs(mLocalAddr.sin_port);
			std::cerr << std::endl;
			std::cerr << "Current Preferred: " << rs_inet_ntoa(prefAddr);
			std::cerr << std::endl;
		}
#endif
		
		// update address.
		mLocalAddr.sin_addr = prefAddr;
		mNetFlags.mLocalAddr = mLocalAddr;

		if(isLoopbackNet(&(mLocalAddr.sin_addr)))
		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgr::checkNetAddress() laddr: Loopback" << std::endl;
#endif
			mNetFlags.mLocalAddrOk = false;
			mNetStatus = RS_NET_LOOPBACK;
		}
		else if (!isValidNet(&mLocalAddr.sin_addr))
		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgr::checkNetAddress() laddr: invalid" << std::endl;
#endif
			mNetFlags.mLocalAddrOk = false;
		}
		else
		{
#ifdef NETMGR_DEBUG_TICK
			std::cerr << "p3NetMgr::checkNetAddress() laddr okay" << std::endl;
#endif
			mNetFlags.mLocalAddrOk = true;
		}


		int port = ntohs(mLocalAddr.sin_port);
		if ((port < PQI_MIN_PORT) || (port > PQI_MAX_PORT))
		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgr::checkNetAddress() Correcting Port to DEFAULT" << std::endl;
#endif
			// Generate a default port from SSL id. The port will always be the
			// same, but appear random from peer to peer.
		 	// Random port avoids clashes, improves anonymity.
			//
			
			mLocalAddr.sin_port = htons(PQI_MIN_PORT + (RSRandom::random_u32() % (PQI_MAX_PORT - PQI_MIN_PORT))); 

			addrChanged = true;
		}

		/* if localaddr = serveraddr, then ensure that the ports
		 * are the same (modify server)... this mismatch can
		 * occur when the local port is changed....
		 */
		if (mLocalAddr.sin_addr.s_addr == mExtAddr.sin_addr.s_addr)
		{
			mExtAddr.sin_port = mLocalAddr.sin_port;
		}

		// ensure that address family is set, otherwise windows Barfs.
		mLocalAddr.sin_family = AF_INET;
		mExtAddr.sin_family = AF_INET;

#ifdef NETMGR_DEBUG_TICK
		std::cerr << "p3NetMgr::checkNetAddress() Final Local Address: " << rs_inet_ntoa(mLocalAddr.sin_addr);
		std::cerr << ":" << ntohs(mLocalAddr.sin_port) << std::endl;
		std::cerr << std::endl;
#endif
		
	}
	
	if (addrChanged)
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgr::checkNetAddress() local address changed, resetting network." << std::endl;
		std::cerr << std::endl;
#endif
		
		//mPeerMgr->UpdateOwnAddress(mLocalAddr, mExtAddr);
		
		netReset();
	}

	return 1;
}


/**********************************************************************************************
 ************************************** Interfaces    *****************************************
 **********************************************************************************************/

/* to allow resets of network stuff */
void    p3NetMgr::addNetListener(pqiNetListener *listener)
{
        RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
        mNetListeners.push_back(listener);
}



bool    p3NetMgr::setLocalAddress(struct sockaddr_in addr)
{
	bool changed = false;
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		if ((mLocalAddr.sin_addr.s_addr != addr.sin_addr.s_addr) ||
		    (mLocalAddr.sin_port != addr.sin_port))
		{
			changed = true;
		}

		mLocalAddr = addr;
	}

	if (changed)
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgr::setLocalAddress() Calling NetReset" << std::endl;
#endif
		netReset();
	}
	return true;
}

bool    p3NetMgr::setExtAddress(struct sockaddr_in addr)
{
	bool changed = false;
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		if ((mExtAddr.sin_addr.s_addr != addr.sin_addr.s_addr) ||
		    (mExtAddr.sin_port != addr.sin_port))
		{
			changed = true;
		}

		mExtAddr = addr;
	}

	if (changed)
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgr::setExtAddress() Calling NetReset" << std::endl;
#endif
		netReset();
	}
	return true;
}

bool    p3NetMgr::setNetworkMode(uint32_t netMode)
{
	uint32_t oldNetMode;
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		/* only change TRY flags */

		oldNetMode = mNetMode;

#ifdef NETMGR_DEBUG
		std::cerr << "p3NetMgr::setNetworkMode()";
		std::cerr << " Existing netMode: " << mNetMode;
		std::cerr << " Input netMode: " << netMode;
		std::cerr << std::endl;
#endif
		mNetMode &= ~(RS_NET_MODE_TRYMODE);

		switch(netMode & RS_NET_MODE_ACTUAL)
		{
			case RS_NET_MODE_EXT:
				mNetMode |= RS_NET_MODE_TRY_EXT;
				break;
			case RS_NET_MODE_UPNP:
				mNetMode |= RS_NET_MODE_TRY_UPNP;
				break;
			default:
			case RS_NET_MODE_UDP:
				mNetMode |= RS_NET_MODE_TRY_UDP;
				break;
		}
	}


	if ((netMode & RS_NET_MODE_ACTUAL) != (oldNetMode & RS_NET_MODE_ACTUAL)) 
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgr::setNetworkMode() Calling NetReset" << std::endl;
#endif
		netReset();
	}
	return true;
}


bool    p3NetMgr::setVisState(uint32_t visState)
{
	uint32_t netMode;
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		mVisState = visState;

		/* if we've started up - then tweak Dht On/Off */
		if (mNetStatus != RS_NET_UNKNOWN)
		{
			enableNetAssistConnect(!(mVisState & RS_VIS_STATE_NODHT));
		}
	}
	return true;
}


/**********************************************************************************************
 ************************************** Interfaces    *****************************************
 **********************************************************************************************/

void p3NetMgr::addNetAssistFirewall(uint32_t id, pqiNetAssistFirewall *fwAgent)
{
	mFwAgents[id] = fwAgent;
}


bool p3NetMgr::enableNetAssistFirewall(bool on)
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		(it->second)->enable(on);
	}
	return true;
}


bool p3NetMgr::netAssistFirewallEnabled()
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		if ((it->second)->getEnabled())
		{
			return true;
		}
	}
	return false;
}

bool p3NetMgr::netAssistFirewallActive()
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		if ((it->second)->getActive())
		{
			return true;
		}
	}
	return false;
}

bool p3NetMgr::netAssistFirewallShutdown()
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		(it->second)->shutdown();
	}
	return true;
}

bool p3NetMgr::netAssistFirewallPorts(uint16_t iport, uint16_t eport)
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		(it->second)->setInternalPort(iport);
		(it->second)->setExternalPort(eport);
	}
	return true;
}


bool p3NetMgr::netAssistExtAddress(struct sockaddr_in &extAddr)
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		if ((it->second)->getActive())
		{
			if ((it->second)->getExternalAddress(extAddr))
			{
				return true;
			}
		}
	}
	return false;
}


void p3NetMgr::addNetAssistConnect(uint32_t id, pqiNetAssistConnect *dht)
{
	mDhts[id] = dht;
}

bool p3NetMgr::enableNetAssistConnect(bool on)
{

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr::enableNetAssistConnect(" << on << ")";
	std::cerr << std::endl;
#endif

	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		(it->second)->enable(on);
	}
	return true;
}

bool p3NetMgr::netAssistConnectEnabled()
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		if ((it->second)->getEnabled())
		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgr::netAssistConnectEnabled() YES";
			std::cerr << std::endl;
#endif

			return true;
		}
	}

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr::netAssistConnectEnabled() NO";
	std::cerr << std::endl;
#endif

	return false;
}

bool p3NetMgr::netAssistConnectActive()
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		if ((it->second)->getActive())

		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgr::netAssistConnectActive() ACTIVE";
			std::cerr << std::endl;
#endif

			return true;
		}
	}

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr::netAssistConnectActive() INACTIVE";
	std::cerr << std::endl;
#endif

	return false;
}

bool p3NetMgr::netAssistConnectStats(uint32_t &netsize, uint32_t &localnetsize)
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		if (((it->second)->getActive()) && ((it->second)->getNetworkStats(netsize, localnetsize)))

		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgr::netAssistConnectStats(";
			std::cerr << netsize << ", " << localnetsize << ")";
			std::cerr << std::endl;
#endif

			return true;
		}
	}

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr::netAssistConnectStats() INACTIVE";
	std::cerr << std::endl;
#endif

	return false;
}

bool p3NetMgr::netAssistConnectShutdown()
{
#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr::netAssistConnectShutdown()";
	std::cerr << std::endl;
#endif

	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		(it->second)->shutdown();
	}
	return true;
}

bool p3NetMgr::netAssistFriend(std::string id, bool on)
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr::netAssistFriend(" << id << ", " << on << ")";
	std::cerr << std::endl;
#endif

	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		if (on)
			(it->second)->findPeer(id);
		else
			(it->second)->dropPeer(id);
	}
	return true;
}


bool p3NetMgr::netAssistSetAddress( struct sockaddr_in &laddr,
					struct sockaddr_in &eaddr,
					uint32_t mode)
{
#if 0
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		(it->second)->setExternalInterface(laddr, eaddr, mode);
	}
#endif
	return true;
}

void p3NetMgr::netAssistConnectTick()
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		(it->second)->tick();
	}
	return;
}



/**********************************************************************
 **********************************************************************
 ******************** Network State ***********************************
 **********************************************************************
 **********************************************************************/

bool    p3NetMgr::getUPnPState()
{
	return netAssistFirewallActive();
}

bool	p3NetMgr::getUPnPEnabled()
{
	return netAssistFirewallEnabled();
}

bool	p3NetMgr::getDHTEnabled()
{
	return netAssistConnectEnabled();
}


void	p3NetMgr::getNetStatus(pqiNetStatus &status)
{
	/* cannot lock local stack, then call DHT... as this can cause lock up */
	/* must extract data... then update mNetFlags */

	bool dhtOk = netAssistConnectActive();
	uint32_t netsize, rsnetsize;
	netAssistConnectStats(netsize, rsnetsize);

	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

	/* quick update of the stuff that can change! */
	mNetFlags.mDhtOk = dhtOk;
	mNetFlags.mDhtNetworkSize = netsize;
	mNetFlags.mDhtRsNetworkSize = rsnetsize;

	status = mNetFlags;
}









/**********************************************************************************************
 ************************************** ExtAddrFinder *****************************************
 **********************************************************************************************/

bool  p3NetMgr::getIPServersEnabled() 
{ 
	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
	return mUseExtAddrFinder;
}

void  p3NetMgr::getIPServersList(std::list<std::string>& ip_servers) 
{ 
	mExtAddrFinder->getIPServersList(ip_servers);
}

void p3NetMgr::setIPServersEnabled(bool b)
{
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		mUseExtAddrFinder = b;
	}

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr: setIPServers to " << b << std::endl ; 
#endif

}

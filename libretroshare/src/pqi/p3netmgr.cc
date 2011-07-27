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
#include "retroshare/rsconfig.h"

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
 * #define NETMGR_DEBUG_STATEBOX 1
 ***/

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


p3NetMgrIMPL::p3NetMgrIMPL()
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

		mLastSlowTickTime = 0;
		mOldNatType = RSNET_NATTYPE_UNKNOWN;
		mOldNatHole = RSNET_NATHOLE_UNKNOWN;
		mLocalAddr.sin_port = 0;
		mLocalAddr.sin_addr.s_addr = 0;
	}
	
#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr() Startup" << std::endl;
#endif

	netReset();

	return;
}

void p3NetMgrIMPL::setManagers(p3PeerMgr *peerMgr, p3LinkMgr *linkMgr)
{
	mPeerMgr = peerMgr;
	mLinkMgr = linkMgr;
}

//void p3NetMgrIMPL::setDhtMgr(p3DhtMgr *dhtMgr)
//{
//	mDhtMgr = dhtMgr;
//}

void p3NetMgrIMPL::setAddrAssist(pqiAddrAssist *dhtStun, pqiAddrAssist *proxyStun)
{
	mDhtStunner = dhtStun;
	mProxyStunner = proxyStun;
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

void p3NetMgrIMPL::netReset()
{
#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgrIMPL::netReset() Called" << std::endl;
#endif

	shutdown(); /* blocking shutdown call */

	// Will initiate a new call for determining the external ip.
	if (mUseExtAddrFinder)
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgrIMPL::netReset() restarting AddrFinder" << std::endl;
#endif
		mExtAddrFinder->reset() ;
	}
	else
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgrIMPL::netReset() ExtAddrFinder Disabled" << std::endl;
#endif
	}

#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgrIMPL::netReset() resetting NetStatus" << std::endl;
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
		std::cerr << "p3NetMgrIMPL::netReset() resetting listeners" << std::endl;
#endif
		std::list<pqiNetListener *>::const_iterator it;
		for(it = mNetListeners.begin(); it != mNetListeners.end(); it++)
		{
			(*it)->resetListener(iaddr);
#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgrIMPL::netReset() reset listener" << std::endl;
#endif
		}
	}

	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
        	mNetStatus = RS_NET_UNKNOWN;
		netStatusReset_locked();
	}

	updateNetStateBox_reset();

#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgrIMPL::netReset() done" << std::endl;
#endif
}


void p3NetMgrIMPL::netStatusReset_locked()
{
	//std::cerr << "p3NetMgrIMPL::netStatusReset()" << std::endl;;

	mNetFlags = pqiNetStatus();
}


bool p3NetMgrIMPL::shutdown() /* blocking shutdown call */
{
#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::shutdown()";
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









void p3NetMgrIMPL::netStartup()
{
	/* startup stuff */

	/* StunInit gets a list of peers, and asks the DHT to find them...
	 * This is needed for all systems so startup straight away 
	 */
#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgrIMPL::netStartup()" << std::endl;
#endif

        netDhtInit(); 

	/* decide which net setup mode we're going into 
	 */


	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

	mNetInitTS = time(NULL);
	netStatusReset_locked();

#ifdef NETMGR_DEBUG_RESET
	std::cerr << "p3NetMgrIMPL::netStartup() resetting mNetInitTS / Status" << std::endl;
#endif
	mNetMode &= ~(RS_NET_MODE_ACTUAL);

	switch(mNetMode & RS_NET_MODE_TRYMODE)
	{

		case RS_NET_MODE_TRY_EXT:  /* v similar to UDP */
#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgrIMPL::netStartup() TRY_EXT mode";
			std::cerr << std::endl;
#endif
			mNetMode |= RS_NET_MODE_EXT;
			mNetStatus = RS_NET_EXT_SETUP;
			break;

		case RS_NET_MODE_TRY_UDP:
#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgrIMPL::netStartup() TRY_UDP mode";
			std::cerr << std::endl;
#endif
			mNetMode |= RS_NET_MODE_UDP;
			mNetStatus = RS_NET_EXT_SETUP;
			break;

		default: // Fall through.

#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgrIMPL::netStartup() UNKNOWN mode";
			std::cerr << std::endl;
#endif

		case RS_NET_MODE_TRY_UPNP:
#ifdef NETMGR_DEBUG_RESET
			std::cerr << "p3NetMgrIMPL::netStartup() TRY_UPNP mode";
			std::cerr << std::endl;
#endif
			/* Force it here (could be default!) */
			mNetMode |= RS_NET_MODE_TRY_UPNP;
			mNetMode |= RS_NET_MODE_UDP;      /* set to UDP, upgraded is UPnP is Okay */
			mNetStatus = RS_NET_UPNP_INIT;
			break;
	}
}


void p3NetMgrIMPL::tick()
{
	time_t now = time(NULL);
	bool doSlowTick = false;
	{
		RsStackMutex stack(mNetMtx);   /************** LOCK MUTEX ***************/
		if (now > mLastSlowTickTime)
		{
			mLastSlowTickTime = now;
			doSlowTick = true;
		}
	}

	if (doSlowTick)
	{
		slowTick();
	}
}


void p3NetMgrIMPL::slowTick()
{
	netTick();
	netAssistConnectTick();
	updateNetStateBox_temporal();

	if (mDhtStunner)
	{
		mDhtStunner->tick();
	}

	if (mProxyStunner)
	{
		mProxyStunner->tick();
	}

}

#define STARTUP_DELAY 5


void p3NetMgrIMPL::netTick()
{

#ifdef NETMGR_DEBUG_TICK
	std::cerr << "p3NetMgrIMPL::netTick()" << std::endl;
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
			std::cerr << "p3NetMgrIMPL::netTick() STATUS: NEEDS_RESET" << std::endl;
#endif
			netReset();
			break;

		case RS_NET_UNKNOWN:
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgrIMPL::netTick() STATUS: UNKNOWN" << std::endl;
#endif

			/* add a small delay to stop restarting straight after a RESET 
			 * This is so can we shutdown cleanly
			 */
			if (age < STARTUP_DELAY)
			{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
				std::cerr << "p3NetMgrIMPL::netTick() Delaying Startup" << std::endl;
#endif
			}
			else
			{
				netStartup();
			}

			break;

		case RS_NET_UPNP_INIT:
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgrIMPL::netTick() STATUS: UPNP_INIT" << std::endl;
#endif
			netUpnpInit();
			break;

		case RS_NET_UPNP_SETUP:
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgrIMPL::netTick() STATUS: UPNP_SETUP" << std::endl;
#endif
			netUpnpCheck();
			break;


		case RS_NET_EXT_SETUP:
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgrIMPL::netTick() STATUS: EXT_SETUP" << std::endl;
#endif
			netExtCheck();
			break;

		case RS_NET_DONE:
#ifdef NETMGR_DEBUG_TICK
			std::cerr << "p3NetMgrIMPL::netTick() STATUS: DONE" << std::endl;
#endif

			break;

		case RS_NET_LOOPBACK:
                        //don't do a shutdown because a client in a computer without local network might be usefull for debug.
                        //shutdown();
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
                        std::cerr << "p3NetMgrIMPL::netTick() STATUS: RS_NET_LOOPBACK" << std::endl;
#endif
		default:
			break;
	}

	return;
}


void p3NetMgrIMPL::netDhtInit()
{
#if defined(NETMGR_DEBUG_RESET)
	std::cerr << "p3NetMgrIMPL::netDhtInit()" << std::endl;
#endif
	
	uint32_t vs = 0;
	{
		RsStackMutex stack(mNetMtx); /*********** LOCKED MUTEX ************/
		vs = mVisState;
	}
	
	enableNetAssistConnect(!(vs & RS_VIS_STATE_NODHT));
}


void p3NetMgrIMPL::netUpnpInit()
{
#if defined(NETMGR_DEBUG_RESET)
	std::cerr << "p3NetMgrIMPL::netUpnpInit()" << std::endl;
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

void p3NetMgrIMPL::netUpnpCheck()
{
	/* grab timestamp */
	mNetMtx.lock();   /*   LOCK MUTEX */

	time_t delta = time(NULL) - mNetInitTS;

#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
		std::cerr << "p3NetMgrIMPL::netUpnpCheck() age: " << delta << std::endl;
#endif

	mNetMtx.unlock(); /* UNLOCK MUTEX */

	struct sockaddr_in extAddr;
	int upnpState = netAssistFirewallActive();

	if (((upnpState == 0) && (delta > (time_t)MAX_UPNP_INIT)) ||
	    ((upnpState > 0) && (delta > (time_t)MAX_UPNP_COMPLETE)))
	{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
		std::cerr << "p3NetMgrIMPL::netUpnpCheck() ";
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
		std::cerr << "p3NetMgrIMPL::netUpnpCheck() ";
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
			std::cerr << "p3NetMgrIMPL::netUpnpCheck() ";
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
		std::cerr << "p3NetMgrIMPL::netUpnpCheck() ";
		std::cerr << "Upnp Check Continues: status: " << upnpState << std::endl;
#endif
	}

}


void p3NetMgrIMPL::netExtCheck()
{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
	std::cerr << "p3NetMgrIMPL::netExtCheck()" << std::endl;
#endif
	bool netSetupDone = false;

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
			std::cerr << "p3NetMgrIMPL::netExtCheck() Ext Not Ok" << std::endl;
#endif

			/* net Assist */
			if (netAssistExtAddress(tmpip))
			{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
				std::cerr << "p3NetMgrIMPL::netExtCheck() Ext supplied from netAssistExternalAddress()" << std::endl;
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
					std::cerr << "p3NetMgrIMPL::netExtCheck() Bad Address supplied from netAssistExternalAddress()" << std::endl;
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
				std::cerr << "p3NetMgrIMPL::netExtCheck() checking ExtAddrFinder" << std::endl;
#endif
				bool extFinderOk = mExtAddrFinder->hasValidIP(&(tmpip.sin_addr));
				if (extFinderOk)
				{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
					std::cerr << "p3NetMgrIMPL::netExtCheck() Ext supplied by ExtAddrFinder" << std::endl;
#endif
					/* best guess at port */
					tmpip.sin_port = mNetFlags.mLocalAddr.sin_port;
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
					std::cerr << "p3NetMgrIMPL::netExtCheck() ";
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
			std::cerr << "p3NetMgrIMPL::netExtCheck() ";
			std::cerr << "ExtAddr: " << rs_inet_ntoa(mNetFlags.mExtAddr.sin_addr);
			std::cerr << ":" << ntohs(mNetFlags.mExtAddr.sin_port);
			std::cerr << std::endl;
#endif
			//update ip address list
			mExtAddr = mNetFlags.mExtAddr;

			mNetStatus = RS_NET_DONE;
			netSetupDone = true;

#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgrIMPL::netExtCheck() Ext Ok: RS_NET_DONE" << std::endl;
#endif



			if (!mNetFlags.mExtAddrStableOk)
			{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
				std::cerr << "p3NetMgrIMPL::netUdpCheck() UDP Unstable :( ";
				std::cerr <<  std::endl;
				std::cerr << "p3NetMgrIMPL::netUdpCheck() We are unreachable";
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
			std::cerr << "p3NetMgrIMPL::netExtCheck() setting netAssistSetAddress()" << std::endl;
#endif
			netAssistSetAddress(mNetFlags.mLocalAddr, mNetFlags.mExtAddr, mNetMode);
		}
#if 0
		else
		{
			std::cerr << "p3NetMgrIMPL::netExtCheck() setting ERR netAssistSetAddress(0)" << std::endl;
			/* mode = 0 for error */
			netAssistSetAddress(mNetFlags.mLocalAddr, mNetFlags.mExtAddr, mNetMode);
		}
#endif

		/* flag unreachables! */
		if ((mNetFlags.mExtAddrOk) && (!mNetFlags.mExtAddrStableOk))
		{
#if defined(NETMGR_DEBUG_TICK) || defined(NETMGR_DEBUG_RESET)
			std::cerr << "p3NetMgrIMPL::netExtCheck() Ext Unstable - Unreachable Check" << std::endl;
#endif
		}



	}

	if (netSetupDone)
	{
		/* Setup NetStateBox with this info */
		updateNetStateBox_startup();
	}

}

/**********************************************************************************************
 ************************************** Interfaces    *****************************************
 **********************************************************************************************/

bool 	p3NetMgrIMPL::checkNetAddress()
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
		std::cerr << "p3NetMgrIMPL::checkNetAddress() no Valid Network Address, resetting network." << std::endl;
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
		std::cerr << "p3NetMgrIMPL::checkNetAddress()";
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
			std::cerr << "p3NetMgrIMPL::checkNetAddress() Address Changed!";
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
			std::cerr << "p3NetMgrIMPL::checkNetAddress() laddr: Loopback" << std::endl;
#endif
			mNetFlags.mLocalAddrOk = false;
			mNetStatus = RS_NET_LOOPBACK;
		}
		else if (!isValidNet(&mLocalAddr.sin_addr))
		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgrIMPL::checkNetAddress() laddr: invalid" << std::endl;
#endif
			mNetFlags.mLocalAddrOk = false;
		}
		else
		{
#ifdef NETMGR_DEBUG_TICK
			std::cerr << "p3NetMgrIMPL::checkNetAddress() laddr okay" << std::endl;
#endif
			mNetFlags.mLocalAddrOk = true;
		}


		int port = ntohs(mLocalAddr.sin_port);
		if ((port < PQI_MIN_PORT) || (port > PQI_MAX_PORT))
		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgrIMPL::checkNetAddress() Correcting Port to DEFAULT" << std::endl;
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
		std::cerr << "p3NetMgrIMPL::checkNetAddress() Final Local Address: " << rs_inet_ntoa(mLocalAddr.sin_addr);
		std::cerr << ":" << ntohs(mLocalAddr.sin_port) << std::endl;
		std::cerr << std::endl;
#endif
		
	}
	
	if (addrChanged)
	{
#ifdef NETMGR_DEBUG_RESET
		std::cerr << "p3NetMgrIMPL::checkNetAddress() local address changed, resetting network." << std::endl;
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
void    p3NetMgrIMPL::addNetListener(pqiNetListener *listener)
{
        RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
        mNetListeners.push_back(listener);
}



bool    p3NetMgrIMPL::setLocalAddress(struct sockaddr_in addr)
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
		std::cerr << "p3NetMgrIMPL::setLocalAddress() Calling NetReset" << std::endl;
#endif
		netReset();
	}
	return true;
}

bool    p3NetMgrIMPL::setExtAddress(struct sockaddr_in addr)
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
		std::cerr << "p3NetMgrIMPL::setExtAddress() Calling NetReset" << std::endl;
#endif
		netReset();
	}
	return true;
}

bool    p3NetMgrIMPL::setNetworkMode(uint32_t netMode)
{
	uint32_t oldNetMode;
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		/* only change TRY flags */

		oldNetMode = mNetMode;

#ifdef NETMGR_DEBUG
		std::cerr << "p3NetMgrIMPL::setNetworkMode()";
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
		std::cerr << "p3NetMgrIMPL::setNetworkMode() Calling NetReset" << std::endl;
#endif
		netReset();
	}
	return true;
}


bool    p3NetMgrIMPL::setVisState(uint32_t visState)
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

void p3NetMgrIMPL::addNetAssistFirewall(uint32_t id, pqiNetAssistFirewall *fwAgent)
{
	mFwAgents[id] = fwAgent;
}


bool p3NetMgrIMPL::enableNetAssistFirewall(bool on)
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		(it->second)->enable(on);
	}
	return true;
}


bool p3NetMgrIMPL::netAssistFirewallEnabled()
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

bool p3NetMgrIMPL::netAssistFirewallActive()
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

bool p3NetMgrIMPL::netAssistFirewallShutdown()
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		(it->second)->shutdown();
	}
	return true;
}

bool p3NetMgrIMPL::netAssistFirewallPorts(uint16_t iport, uint16_t eport)
{
	std::map<uint32_t, pqiNetAssistFirewall *>::iterator it;
	for(it = mFwAgents.begin(); it != mFwAgents.end(); it++)
	{
		(it->second)->setInternalPort(iport);
		(it->second)->setExternalPort(eport);
	}
	return true;
}


bool p3NetMgrIMPL::netAssistExtAddress(struct sockaddr_in &extAddr)
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


void p3NetMgrIMPL::addNetAssistConnect(uint32_t id, pqiNetAssistConnect *dht)
{
	mDhts[id] = dht;
}

bool p3NetMgrIMPL::enableNetAssistConnect(bool on)
{

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::enableNetAssistConnect(" << on << ")";
	std::cerr << std::endl;
#endif

	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		(it->second)->enable(on);
	}
	return true;
}

bool p3NetMgrIMPL::netAssistConnectEnabled()
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		if ((it->second)->getEnabled())
		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgrIMPL::netAssistConnectEnabled() YES";
			std::cerr << std::endl;
#endif

			return true;
		}
	}

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::netAssistConnectEnabled() NO";
	std::cerr << std::endl;
#endif

	return false;
}

bool p3NetMgrIMPL::netAssistConnectActive()
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		if ((it->second)->getActive())

		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgrIMPL::netAssistConnectActive() ACTIVE";
			std::cerr << std::endl;
#endif

			return true;
		}
	}

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::netAssistConnectActive() INACTIVE";
	std::cerr << std::endl;
#endif

	return false;
}

bool p3NetMgrIMPL::netAssistConnectStats(uint32_t &netsize, uint32_t &localnetsize)
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		if (((it->second)->getActive()) && ((it->second)->getNetworkStats(netsize, localnetsize)))

		{
#ifdef NETMGR_DEBUG
			std::cerr << "p3NetMgrIMPL::netAssistConnectStats(";
			std::cerr << netsize << ", " << localnetsize << ")";
			std::cerr << std::endl;
#endif

			return true;
		}
	}

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::netAssistConnectStats() INACTIVE";
	std::cerr << std::endl;
#endif

	return false;
}

bool p3NetMgrIMPL::netAssistConnectShutdown()
{
#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::netAssistConnectShutdown()";
	std::cerr << std::endl;
#endif

	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;
	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		(it->second)->shutdown();
	}
	return true;
}

bool p3NetMgrIMPL::netAssistFriend(std::string id, bool on)
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::netAssistFriend(" << id << ", " << on << ")";
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


bool p3NetMgrIMPL::netAssistAttach(bool on)
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::netAssistAttach(" << on << ")";
	std::cerr << std::endl;
#endif

	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		(it->second)->setAttachMode(on);
	}
	return true;
}



bool p3NetMgrIMPL::netAssistStatusUpdate(std::string id, int state)
{
	std::map<uint32_t, pqiNetAssistConnect *>::iterator it;

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgrIMPL::netAssistFriend(" << id << ", " << on << ")";
	std::cerr << std::endl;
#endif

	for(it = mDhts.begin(); it != mDhts.end(); it++)
	{
		(it->second)->ConnectionFeedback(id, state);
	}
	return true;
}


bool p3NetMgrIMPL::netAssistSetAddress( struct sockaddr_in &laddr,
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

void p3NetMgrIMPL::netAssistConnectTick()
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

bool    p3NetMgrIMPL::getUPnPState()
{
	return netAssistFirewallActive();
}

bool	p3NetMgrIMPL::getUPnPEnabled()
{
	return netAssistFirewallEnabled();
}

bool	p3NetMgrIMPL::getDHTEnabled()
{
	return netAssistConnectEnabled();
}


void	p3NetMgrIMPL::getNetStatus(pqiNetStatus &status)
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

bool  p3NetMgrIMPL::getIPServersEnabled() 
{ 
	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
	return mUseExtAddrFinder;
}

void  p3NetMgrIMPL::getIPServersList(std::list<std::string>& ip_servers) 
{ 
	mExtAddrFinder->getIPServersList(ip_servers);
}

void p3NetMgrIMPL::setIPServersEnabled(bool b)
{
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		mUseExtAddrFinder = b;
	}

#ifdef NETMGR_DEBUG
	std::cerr << "p3NetMgr: setIPServers to " << b << std::endl ; 
#endif

}



/**********************************************************************************************
 ************************************** NetStateBox  ******************************************
 **********************************************************************************************/

uint32_t p3NetMgrIMPL::getNetStateMode()
{
	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
	return mNetStateBox.getNetStateMode();
}

uint32_t p3NetMgrIMPL::getNetworkMode()
{
	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
	return mNetStateBox.getNetworkMode();
}

uint32_t p3NetMgrIMPL::getNatTypeMode()
{
	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
	return mNetStateBox.getNatTypeMode();
}

uint32_t p3NetMgrIMPL::getNatHoleMode()
{
	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
	return mNetStateBox.getNatHoleMode();
}

uint32_t p3NetMgrIMPL::getConnectModes()
{
	RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
	return mNetStateBox.getConnectModes();
}


/* These are the regular updates from Dht / Stunners */
void p3NetMgrIMPL::updateNetStateBox_temporal()
{
#ifdef	NETMGR_DEBUG_STATEBOX
	std::cerr << "p3NetMgrIMPL::updateNetStateBox_temporal() ";
	std::cerr << std::endl;
#endif

	uint8_t isstable = 0;
	struct sockaddr_in tmpaddr;
	sockaddr_clear(&tmpaddr);

	if (mDhtStunner)
	{

        	/* input network bits */
       		if (mDhtStunner->getExternalAddr(tmpaddr, isstable))
		{
			RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
			mNetStateBox.setAddressStunDht(&tmpaddr, isstable);
			
#ifdef	NETMGR_DEBUG_STATEBOX
			std::cerr << "p3NetMgrIMPL::updateNetStateBox_temporal() DhtStunner: ";
			std::cerr << rs_inet_ntoa(tmpaddr.sin_addr) << ":" << htons(tmpaddr.sin_port);
			std::cerr << " Stable: " << (uint32_t) isstable;
			std::cerr << std::endl;
#endif
			
		}
	}

	if (mProxyStunner)
	{

        	/* input network bits */
       		if (mProxyStunner->getExternalAddr(tmpaddr, isstable))
		{
			RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
			mNetStateBox.setAddressStunProxy(&tmpaddr, isstable);

#ifdef	NETMGR_DEBUG_STATEBOX
			std::cerr << "p3NetMgrIMPL::updateNetStateBox_temporal() ProxyStunner: ";
			std::cerr << rs_inet_ntoa(tmpaddr.sin_addr) << ":" << htons(tmpaddr.sin_port);
			std::cerr << " Stable: " << (uint32_t) isstable;
			std::cerr << std::endl;
#endif
		
		}
	}


	{
		bool dhtOn = netAssistConnectEnabled();
		bool dhtActive = netAssistConnectActive();

		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/
		mNetStateBox.setDhtState(dhtOn, dhtActive);
	}


	/* now we check if a WebIP address is required? */


	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

		uint32_t netstate = mNetStateBox.getNetStateMode();
		uint32_t netMode = mNetStateBox.getNetworkMode();
		uint32_t natType = mNetStateBox.getNatTypeMode();
		uint32_t natHole = mNetStateBox.getNatHoleMode();
		uint32_t connect = mNetStateBox.getConnectModes();

		std::string netstatestr = NetStateNetStateString(netstate);
		std::string connectstr = NetStateConnectModesString(connect);
		std::string natholestr = NetStateNatHoleString(natHole);
		std::string nattypestr = NetStateNatTypeString(natType);
		std::string netmodestr = NetStateNetworkModeString(netMode);

#ifdef	NETMGR_DEBUG_STATEBOX
		std::cerr << "p3NetMgrIMPL::updateNetStateBox_temporal() NetStateBox Thinking";
		std::cerr << std::endl;
		std::cerr << "\tNetState: " << netstatestr;
		std::cerr << std::endl;
		std::cerr << "\tConnectModes: " << netstatestr;
		std::cerr << std::endl;
		std::cerr << "\tNetworkMode: " << netmodestr;
		std::cerr << std::endl;
		std::cerr << "\tNatHole: " << natholestr;
		std::cerr << std::endl;
		std::cerr << "\tNatType: " << nattypestr;
		std::cerr << std::endl;
#endif

	}
	
	
	updateNatSetting();

}

#define NET_STUNNER_PERIOD_FAST		(-1)	// default of Stunner.
#define NET_STUNNER_PERIOD_SLOW		(120) 	// This needs to be as small Routers will allow... try 2 minutes.
// FOR TESTING ONLY.
//#define NET_STUNNER_PERIOD_SLOW		(60) 	// 3 minutes.

void p3NetMgrIMPL::updateNatSetting()
{
	bool updateRefreshRate = false;
	uint32_t natType = RSNET_NATTYPE_UNKNOWN;
	uint32_t natHole = RSNET_NATHOLE_UNKNOWN;
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

		natType = mNetStateBox.getNatTypeMode();
		natHole = mNetStateBox.getNatHoleMode();
		if ((natType != mOldNatType) || (natHole != mOldNatHole))
		{
			mOldNatType = natType;
			mOldNatHole = natHole;
			updateRefreshRate = true;
			
#ifdef	NETMGR_DEBUG_STATEBOX
			std::cerr << "p3NetMgrIMPL::updateNetStateBox_temporal() NatType Change!";
			std::cerr << "\tNatType: " << NetStateNatTypeString(natType);
			std::cerr << "\tNatHole: " << NetStateNatHoleString(natHole);

			std::cerr << std::endl;
#endif
			
			
		}
	}

	
	// MUST also use this chance to set ATTACH flag for DHT.
	if (updateRefreshRate)
	{
#ifdef	NETMGR_DEBUG_STATEBOX
		std::cerr << "p3NetMgrIMPL::updateNetStateBox_temporal() Updating Refresh Rate, based on changed NatType";
		std::cerr << std::endl;
#endif
		
		
		switch(natType)
		{
			case RSNET_NATTYPE_RESTRICTED_CONE: 
			{
				if ((natHole == RSNET_NATHOLE_NONE) || (natHole == RSNET_NATHOLE_UNKNOWN))
				{
					mProxyStunner->setRefreshPeriod(NET_STUNNER_PERIOD_FAST);
				}
				else 
				{
					mProxyStunner->setRefreshPeriod(NET_STUNNER_PERIOD_SLOW);
				}
				break;
			}
			case RSNET_NATTYPE_NONE:
			case RSNET_NATTYPE_UNKNOWN:
			case RSNET_NATTYPE_SYMMETRIC:
			case RSNET_NATTYPE_DETERM_SYM:
			case RSNET_NATTYPE_FULL_CONE:
			case RSNET_NATTYPE_OTHER:

				mProxyStunner->setRefreshPeriod(NET_STUNNER_PERIOD_SLOW);
				break;
		}


		/* This controls the Attach mode of the DHT... 
		 * which effectively makes the DHT "attach" to Open Nodes.
		 * So that messages can get through.
		 * We only want to be attached - if we don't have a stable DHT port.
		 */
		if ((natHole == RSNET_NATHOLE_NONE) || (natHole == RSNET_NATHOLE_UNKNOWN))
		{
			switch(natType)
			{
				/* switch to attach mode if we have a bad firewall */
				case RSNET_NATTYPE_UNKNOWN:
				case RSNET_NATTYPE_SYMMETRIC:
				case RSNET_NATTYPE_RESTRICTED_CONE: 
				case RSNET_NATTYPE_DETERM_SYM:
				case RSNET_NATTYPE_OTHER:
					netAssistAttach(true);

				break;
				/* switch off attach mode if we have a nice firewall */
				case RSNET_NATTYPE_NONE:
				case RSNET_NATTYPE_FULL_CONE:
					netAssistAttach(false);
				break;
			}
		}
		else
		{
			// Switch off Firewall Mode (Attach)
			netAssistAttach(false);
		}
	}
}



void p3NetMgrIMPL::updateNetStateBox_startup()
{
#ifdef	NETMGR_DEBUG_STATEBOX
	std::cerr << "p3NetMgrIMPL::updateNetStateBox_startup() ";
	std::cerr << std::endl;
#endif
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

		/* fill in the data */
		struct sockaddr_in tmpip;
	
		/* net Assist */
		if (netAssistExtAddress(tmpip))
		{

#ifdef	NETMGR_DEBUG_STATEBOX
			std::cerr << "p3NetMgrIMPL::updateNetStateBox_startup() ";
			std::cerr << "Ext supplied from netAssistExternalAddress()";
			std::cerr << std::endl;
#endif

			if (isValidNet(&(tmpip.sin_addr)))
			{
#ifdef	NETMGR_DEBUG_STATEBOX
				std::cerr << "p3NetMgrIMPL::updateNetStateBox_startup() ";
				std::cerr << "netAssist Returned: " << rs_inet_ntoa(tmpip.sin_addr);
				std::cerr << ":" << ntohs(tmpip.sin_port);
				std::cerr << std::endl;
#endif
				mNetStateBox.setAddressUPnP(true, &tmpip);
			}
			else
			{
				mNetStateBox.setAddressUPnP(false, &tmpip);
#ifdef	NETMGR_DEBUG_STATEBOX
				std::cerr << "p3NetMgrIMPL::updateNetStateBox_startup() ";
				std::cerr << "ERROR Bad Address supplied from netAssistExternalAddress()";
				std::cerr << std::endl;
#endif
			}
		}
		else
		{
#ifdef	NETMGR_DEBUG_STATEBOX
			std::cerr << "p3NetMgrIMPL::updateNetStateBox_startup() ";
			std::cerr << " netAssistExtAddress() is not active";
			std::cerr << std::endl;
#endif
			mNetStateBox.setAddressUPnP(false, &tmpip);
		}


		/* ExtAddrFinder */
		if (mUseExtAddrFinder)
		{
			bool extFinderOk = mExtAddrFinder->hasValidIP(&(tmpip.sin_addr));
			if (extFinderOk)
			{
				/* best guess at port */
				tmpip.sin_port = mNetFlags.mLocalAddr.sin_port;

#ifdef	NETMGR_DEBUG_STATEBOX
				std::cerr << "p3NetMgrIMPL::updateNetStateBox_startup() ";
				std::cerr << "ExtAddrFinder Returned: " << rs_inet_ntoa(tmpip.sin_addr);
				std::cerr << std::endl;
#endif

				mNetStateBox.setAddressWebIP(true, &tmpip);
			}
			else
			{
				mNetStateBox.setAddressWebIP(false, &tmpip);
#ifdef	NETMGR_DEBUG_STATEBOX
				std::cerr << "p3NetMgrIMPL::updateNetStateBox_startup() ";
				std::cerr << " ExtAddrFinder hasn't found an address yet";
				std::cerr << std::endl;
#endif
			}
		}
		else
		{
#ifdef	NETMGR_DEBUG_STATEBOX
			std::cerr << "p3NetMgrIMPL::updateNetStateBox_startup() ";
			std::cerr << " ExtAddrFinder is not active";
			std::cerr << std::endl;
#endif
			mNetStateBox.setAddressWebIP(false, &tmpip);
		}
	}
}

void p3NetMgrIMPL::updateNetStateBox_reset()
{
	{
		RsStackMutex stack(mNetMtx); /****** STACK LOCK MUTEX *******/

		mNetStateBox.reset();
	}
}

	

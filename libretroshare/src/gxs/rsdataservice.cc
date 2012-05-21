#include <fstream>

#include "rsdataservice.h"

#define MSG_TABLE_NAME std::string("MESSAGES")
#define GRP_TABLE_NAME std::string("GROUPS")


// generic
#define KEY_NXS_FILE std::string("msgFile")
#define KEY_NXS_FILE_OFFSET std::string("fileOffset")
#define KEY_NXS_LEN std::string("msgLen")
#define KEY_NXS_IDENTITY std::string("identity")
#define KEY_GRP_ID std::string("grpId")
#define KEY_IDENTITY_SIGN std::string("idSign")
#define KEY_TIME_STAMP std::string("timeStamp")
#define KEY_NXS_FLAGS std::string("flags")

// grp table columns
#define KEY_ADMIN_SIGN std::string("adminSign")
#define KEY_PUB_PUBLISH_KEY std::string("pubPublishhKey")
#define KEY_PUB_ADMIN_KEY std::string("pubAdminKey")
#define KEY_PRIV_ADMIN_KEY std::string("privAdminKey")
#define KEY_PRIV_PUBLISH_KEY std::string("privPublishKey")
#define KEY_GRP_FILE std::string("grpFile")


// msg table columns
#define KEY_PUBLISH_SIGN std::string("publishSign")
#define KEY_MSG_ID std::string("msgId")


// grp col numbers
#define COL_GRP_ID 1
#define COL_ADMIN_SIGN 2
#define COL_PUB_PUBLISH_KEY 10
#define COL_PUB_ADMIN_KEY 11
#define COL_PRIV_ADMIN_KEY 12
#define COL_PRIV_PUBLISH_KEY 13


// msg col numbers
#define COL_MSG_ID 1
#define COL_PUBLISH_SIGN 2

// generic col numbers
#define COL_NXS_FILE 3
#define COL_NXS_FILE_OFFSET 4
#define COL_NXS_LEN 5
#define COL_TIME_STAMP 6
#define COL_NXS_FLAGS 7
#define COL_IDENTITY_SIGN 8
#define COL_IDENTITY 9


RsDataService::RsDataService(const std::string &serviceDir, const std::string &dbName, uint16_t serviceType,
                             RsGxsSearchModule *mod)
    : mServiceDir(serviceDir), mDbName(dbName), mServType(serviceType){


    // initialise database
    remove("RsFileGdp_DataBase");
    mDb = new RetroDb(dbName, RetroDb::OPEN_READWRITE_CREATE);

    // create table for msgs
    mDb->execSQL("CREATE TABLE " + MSG_TABLE_NAME + "(" + KEY_MSG_ID
                 + " TEXT PRIMARY KEY ASC," + KEY_GRP_ID +  " TEXT," + KEY_NXS_FLAGS + " INT,"
                  + KEY_TIME_STAMP + " INT," + KEY_PUBLISH_SIGN + " BLOB," + KEY_NXS_IDENTITY + " TEXT,"
                 + KEY_IDENTITY_SIGN + " BLOB," + KEY_NXS_FILE + " TEXT,"+ KEY_NXS_FILE_OFFSET + " INT,"
                 + KEY_NXS_LEN+ " INT);");

    // create table for grps
    mDb->execSQL("CREATE TABLE " + GRP_TABLE_NAME + "(" + KEY_GRP_ID +
                 " TEXT PRIMARY KEY ASC," + KEY_TIME_STAMP + " INT," +
                 KEY_ADMIN_SIGN + " BLOB," + KEY_PUB_ADMIN_KEY + " BLOB,"
                 + KEY_PUB_PUBLISH_KEY + " BLOB," + KEY_PRIV_ADMIN_KEY +
                 " BLOB," + KEY_PRIV_PUBLISH_KEY + " BLOB," + KEY_NXS_FILE +
                  " TEXT," + KEY_NXS_FILE_OFFSET + " INT," + KEY_NXS_LEN + " INT,"
                  + KEY_NXS_IDENTITY + " TEXT," + KEY_NXS_FLAGS + " INT," + KEY_IDENTITY_SIGN + " BLOB);");


    msgColumns.push_back(KEY_MSG_ID); msgColumns.push_back(KEY_PUBLISH_SIGN);  msgColumns.push_back(KEY_NXS_FILE);
    msgColumns.push_back(KEY_NXS_FILE_OFFSET); msgColumns.push_back(KEY_NXS_LEN); msgColumns.push_back(KEY_TIME_STAMP);
    msgColumns.push_back(KEY_NXS_FLAGS); msgColumns.push_back(KEY_NXS_IDENTITY); msgColumns.push_back(KEY_IDENTITY_SIGN);

    grpColumns.push_back(KEY_GRP_ID); grpColumns.push_back(KEY_ADMIN_SIGN); grpColumns.push_back(KEY_NXS_FILE);
    grpColumns.push_back(KEY_NXS_FILE_OFFSET); grpColumns.push_back(KEY_NXS_LEN); grpColumns.push_back(KEY_TIME_STAMP);
    grpColumns.push_back(KEY_NXS_FLAGS); grpColumns.push_back(KEY_NXS_IDENTITY); grpColumns.push_back(KEY_IDENTITY_SIGN);
    grpColumns.push_back(KEY_PUB_PUBLISH_KEY); grpColumns.push_back(KEY_PUB_ADMIN_KEY); grpColumns.push_back(KEY_PRIV_ADMIN_KEY);
    grpColumns.push_back(KEY_PRIV_PUBLISH_KEY); grpColumns.push_back(KEY_GRP_FILE);
}


RsNxsGrp* RsDataService::getGroup(RetroCursor &c){

    /*!
     * grpId, pub admin and pub publish key
     * necessary for successful group
     */

    RsNxsGrp* grp = new RsNxsGrp(mServType);
    bool ok = true;

    uint32_t offset = 0;
    // declare members of nxsgrp
    char* data = NULL;
    uint32_t data_len = 0;

    // grpId
    c.getString(COL_GRP_ID, grp->grpId);
    ok &= !grp->grpId.empty();

    // identity if any
    c.getString(COL_IDENTITY, grp->identity);
    grp->timeStamp = c.getInt64(COL_TIME_STAMP);

    if(!grp->identity.empty() && ok){
        offset = 0;
        data = (char*)c.getData(COL_IDENTITY_SIGN, data_len);
        if(data){
            grp->adminSign.GetTlv(data, data_len, &offset);
        }
    }

    grp->grpFlag = c.getInt32(COL_NXS_FLAGS);


    offset = 0; data = NULL; data_len = 0;

    data = c.getData(COL_PRIV_ADMIN_KEY, data_len);
    if(data){
        ok &= grp->keys.SetTlv(data, data_len, &offset);
    }

    std::string grpFile;
    c.getString(COL_NXS_FILE, grpFile);
    data_len = c.getInt32(COL_NXS_LEN);
    ok &= !grpFile.empty();
    /* now retrieve grp data from file */

    if(ok){
        offset = c.getInt32(COL_NXS_FILE_OFFSET);
        std::ifstream istrm(grpFile.c_str());
        istrm.open(grpFile.c_str(), std::ios::binary);
        istrm.seekg(offset, std::ios::beg);
        char grp_data[data_len];
        istrm.read(grp_data, data_len);
        istrm.close();
        offset = 0;
        ok &= grp->grp.SetTlv(grp_data, data_len, &offset);
    }

    if(ok)
        return grp;
    else
        delete grp;

    return NULL;
}


RsNxsMsg* RsDataService::getMessage(RetroCursor &c){

    RsNxsMsg* msg = new RsNxsMsg(mServType);

    bool ok = true;
    uint32_t data_len = 0,
    offset = 0;
    char* data = NULL;
    c.getString(COL_GRP_ID, msg->grpId);
    c.getString(COL_MSG_ID, msg->msgId);

    ok &= (!msg->grpId.empty()) && (!msg->msgId.empty());

    c.getString(COL_IDENTITY, msg->identity);

    if(!msg->identity.empty()){
        data = (char*)c.getData(COL_IDENTITY_SIGN, data_len);
        msg->idSign.SetTlv(data, data_len, &offset);
    }

    msg->msgFlag = c.getInt32(COL_NXS_FLAGS);
    msg->timeStamp = c.getInt32(COL_TIME_STAMP);

    offset = 0; data_len = 0;
    if(ok){

        data = c.getData(COL_PUBLISH_SIGN, data_len);
        if(data)
            msg->publishSign.SetTlv(data, data_len, &offset);

    }

    std::string grpFile;
    c.getString(COL_NXS_FILE, grpFile);
    data_len = c.getInt32(COL_NXS_LEN);
    ok &= !msgFile.empty();
    /* now retrieve grp data from file */

    if(ok){
        offset = c.getInt32(COL_NXS_FILE_OFFSET);
        std::ifstream istrm(grpFile.c_str());
        istrm.open(grpFile.c_str(), std::ios::binary);
        istrm.seekg(offset, std::ios::beg);
        char grp_data[data_len];
        istrm.read(grp_data, data_len);
        istrm.close();
        offset = 0;
        ok &= grp->grp.SetTlv(grp_data, data_len, &offset);
    }

    if(ok)
        return msg;
    else
        delete msg;

    return NULL;
}

int RsDataService::storeMessage(std::set<RsNxsMsg *> &msg){


    std::set<RsNxsMsg*>::iterator sit = msg.begin();

    mDb->execSQL("BEGIN;");


    for(; sit != msg.end(); sit++){

        RsNxsMsg* msgPtr = *sit;
        std::string msgFile = mServiceDir + "/" + msgPtr->grpId + "-msgs";
        std::fstream ostrm(msgFile.c_str(), std::ios::binary | std::ios::app | std::ios::out);
        ostrm.seekg(0, std::ios::end); // go to end to append
        int32_t offset = ostrm.tellg(); // get fill offset

        ContentValue cv;
        cv.put(KEY_MSG_ID, msgPtr->msgId);
        cv.put(KEY_GRP_ID, msgPtr->grpId);
        char pubSignData[msgPtr->publishSign.TlvSize()];
        cv.put(KEY_PUBLISH_SIGN, msgPtr->publishSign.TlvSize(), pubSignData);

        if(! (msgPtr->identity.empty()) ){
            char idSignData[msgPtr->idSign.TlvSize()];
            cv.put(KEY_IDENTITY_SIGN, msgPtr->idSign.TlvSize(), idSignData);
            cv.put(KEY_NXS_IDENTITY, msgPtr->identity);
        }

        cv.put(KEY_NXS_FLAGS, (int32_t) msgPtr->msgFlag);
        cv.put(KEY_TIME_STAMP, (int32_t) msgPtr->timeStamp);

        char msgData[msgPtr->msg.TlvSize()];
        msgPtr->msg.SetTlv(msgData, msgPtr->msg.TlvSize(), offset);
        ostrm.write(msgData, msgPtr->msg.TlvSize());
        ostrm.close();

        cv.put(KEY_NXS_FILE_OFFSET, offset);

        mDb->sqlInsert(MSG_TABLE_NAME, "", cv);
    }

    return mDb->execSQL("COMMIT;");;
}


int RsDataService::storeGroup(std::set<RsNxsGrp *> &grp){


    std::set<RsNxsGrp*>::iterator sit = grp.begin();

    mDb->execSQL("BEGIN;");

    for(; sit != grp.end(); sit++){

        RsNxsGrp* grpPtr = *sit;

        std::string grpFile = mServiceDir + "/" + msgPtr->grpId;
        std::fstream ostrm(grpFile.c_str(), std::ios::binary | std::ios::app | std::ios::out);
        ostrm.seekg(0, std::ios::end); // go to end to append
        int32_t offset = ostrm.tellg(); // get fill offset

        ContentValue cv;
        cv.put(KEY_GRP_ID, grpPtr->grpId);
        cv.put(KEY_NXS_FLAGS, (int32_t)grpPtr->grpFlag);
        cv.put(KEY_TIME_STAMP, (int32_t)grpPtr->timeStamp);

        if(! (grpPtr->identity.empty()) ){
            cv.put(KEY_NXS_IDENTITY, grpPtr->identity);

            char idSignData[msgPtr->idSign.TlvSize()];
            cv.put(KEY_IDENTITY_SIGN, grpPtr->idSign.TlvSize(), idSignData);
        }

        char adminSignData[grpPtr->adminSign.TlvSize()];
        grpPtr->adminSign.SetTlv(adminSignData, grpPtr->adminSign.TlvSize(), offset);
        cv.put(KEY_ADMIN_SIGN, grpPtr->adminSign.TlvSize());

        char keySetData[grpPtr->keys.TlvSize()];
        grpPtr->keys.SetTlv(data, grpPtr->keys.TlvSize(), offset);
        cv.put(KEY_KEY_SET, grpPtr->keys.TlvSize(), keySetData);

        char grpData[grpPtr->grp.TlvSize()];
        msgPtr->msg.SetTlv(grpData, grpPtr->grp.TlvSize(), offset);
        ostrm.write(grpData, grpPtr->grp.TlvSize());
        ostrm.close();
    }

    return mDb->execSQL("COMMIT;");
}

int RsDataService::retrieveGrps(std::map<std::string, RsNxsGrp*> &grp, bool cache){

    RetroCursor* c = mDb->sqlQuery(GRP_TABLE_NAME, grpColumns, "", "");

    if(c){

        bool valid = c->moveToFirst();

        while(valid){
            RsNxsGrp* g = getGroup(*c);


            // only add the latest grp info
            bool exists = grp.find(g->grpId) != grp.end();
            if(exists){

                if(grp[g->grpId]->timeStamp < g->timeStamp){
                    delete grp[g->grpId];
                    grp.insert(g);
                }else{
                    delete g;
                }
            }else{
                grp.insert(g);
            }

            valid = c->moveToNext();
        }

        delete c;
        return 1;
    }else{
        return 0;
    }
}

int RsDataService::retrieveMsgs(const std::string &grpId, std::map<std::string, RsGxsMsg *> msg, bool cache){

    RetroCursor* c = mDb->sqlQuery(MSG_TABLE_NAME, msgColumns, KEY_GRP_ID+ "=" + grpId, "");

    if(c){

        bool valid = c->moveToFirst();
        while(valid){
            RsNxsMsg* m = getMessage(*c);

            // only add the latest grp info
            bool exists = grp.find(g->grpId) != grp.end();
            if(exists){

                if(grp[m->msgId]->timeStamp < m->timeStamp){
                    delete msg[m->msgId];
                    msg[m->msgId] = m;
                }else{
                    delete m;
                }
            }else{
                msg[m->msgId] = m;
            }

            valid = c->moveToNext();
        }

        delete c;
        return 1;
    }else{
        return 0;
    }
}

int RsDataService::retrieveMsgVersions(const std::string &grpId, const std::string msgId,
                                       std::set<RsNxsMsg *> msg, bool cache){


    std::string selection = KEY_GRP_ID + "=" + grpId + "," + KEY_MSG_ID + "=" + msgId;
    RetroCursor* c = mDb->sqlQuery(MSG_TABLE_NAME, msgColumns, selection, "");


    if(c){

        bool valid = c->moveToFirst();
        while(valid){
            RsNxsMsg* m = getMessage(*c);

            if(m)
                msg.insert(m);

            valid = c->moveToNext();
        }

        delete c;
        return 1;
    }else{
        return 0;
    }

}

int RsDataService::retrieveGrpVersions(const std::string &grpId, std::set<RsGxsGroup *> &grp){

    std::string selection = KEY_GRP_ID + "=" + grpId;
    RetroCursor* c = mDb->sqlQuery(GRP_TABLE_NAME, msgColumns, selection, "");

    if(c){

        bool valid = c->moveToFirst();
        while(valid){
            RsNxsGrp* g = getGroup(*c);

            if(g)
                grp.insert(g);

            valid = c->moveToNext();
        }

        delete c;
        return 1;
    }else{
        return 0;
    }
}

RsNxsGrp* RsDataService::retrieveGrpVersion(const RsGxsGrpId &grpId){

    std::set<RsNxsGrp*> grps;
    retrieveGrpVersions(grpId.grpId, grps, false);
    RsNxsGrp* grp = NULL;

    if(!grps.empty()){

        // find grp with comparable sign
        std::set<RsNxsGrp*>::iterator sit = grps.begin();

        for(; sit != grps.end(); sit++){
            grp = *sit;
            if(grp->adminSign == grpId.adminSign){
                break;
            }
            grp = NULL;
        }

        if(grp){
            grps.erase(grp);
            // release memory for non matching grps
            for(sit = grps.begin(); sit != grps.end(); sit++)
                delete *sit;
        }

    }

    return grp;
}

RsNxsMsg* RsDataService::retrieveMsgVersion(const RsGxsMsgId &msgId){

    std::set<RsNxsMsg*> msgs;
    retrieveMsgVersions(msgId.grpId, msgId.msgId, msgs);
    RsNxsMsg* msg = NULL;

    if(!msgs.empty()){

        std::set<RsNxsMsg*>::iterator sit = msgs.begin();

        for(; sit != msgs.end(); sit++){

            msg = *sit;
            if(msg->idSign == msgId.idSign)
                break;

            msg = NULL;
        }

        if(msg){
            msgs.erase(msg);

            for(sit = msgs.begin(); sit != msgs.end(); sit++)
                delete *sit;
        }
    }


    return msg;
}

uint32_t RsDataService::cacheSize() const {
    return 0;
}

int RsDataService::setCacheSize(uint32_t size) {
    return 0;
}

int RsDataService::searchGrps(RsGxsSearch *search, std::list<RsGxsSrchResGrpCtx *> &result) {

    return 0;
}

int RsDataService::searchMsgs(RsGxsSearch *search, std::list<RsGxsSrchResMsgCtx *> &result) {
    return 0;
}

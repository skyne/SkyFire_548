/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "ClusterAdminHttp.h"
#include "Common.h"
#include "Configuration/Config.h"
#include "Cryptography/OpenSSLProviders.h"
#include "Database/DatabaseEnv.h"
#include "GatewayAcceptor.h"
#include "Log.h"
#include "Platform/TimeUtils.h"
#include "SystemConfig.h"
#include "Util.h"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <mysql.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#ifndef _SKYFIRE_GATEWAY_CONFIG
# define _SKYFIRE_GATEWAY_CONFIG "clustergateway.conf"
#endif

LoginDatabaseWorkerPool LoginDatabase;
CharacterDatabaseWorkerPool CharacterDatabase;
bool characterDatabaseOpen = false;

bool stopEvent = false;

void GatewaySignalHandler(int)
{
    stopEvent = true;
}

bool StartDB()
{
    MySQL::Library_Init();
    std::string dbstring = sConfigMgr->GetStringDefault("LoginDatabaseInfo", "");
    if (dbstring.empty())
    {
        SF_LOG_ERROR("server.clustergateway", "LoginDatabaseInfo is empty.");
        return false;
    }

    int32 workers = sConfigMgr->GetIntDefault("LoginDatabase.WorkerThreads", 1);
    int32 synch = sConfigMgr->GetIntDefault("LoginDatabase.SynchThreads", 1);
    if (workers < 1 || workers > 32)
        workers = 1;
    if (synch < 1 || synch > 32)
        synch = 1;

    if (!LoginDatabase.Open(dbstring, uint8(workers), uint8(synch)))
    {
        SF_LOG_ERROR("server.clustergateway", "Cannot connect to login database.");
        return false;
    }

    std::string charDb = sConfigMgr->GetStringDefault("CharacterDatabaseInfo", "");
    if (!charDb.empty())
    {
        if (CharacterDatabase.Open(charDb, uint8(workers), uint8(synch)))
        {
            characterDatabaseOpen = true;
            ClusterAdminHttpSetCharacterDatabase(true);
        }
        else
            SF_LOG_ERROR("server.clustergateway", "Cannot connect to character database; /v1/players will be empty.");
    }

    return true;
}

void StopDB()
{
    ClusterAdminHttpSetCharacterDatabase(false);
    if (characterDatabaseOpen)
    {
        CharacterDatabase.Close();
        characterDatabaseOpen = false;
    }
    LoginDatabase.Close();
    MySQL::Library_End();
}

extern int main(int argc, char** argv)
{
    char const* configFile = _SKYFIRE_GATEWAY_CONFIG;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-c") && i + 1 < argc)
            configFile = argv[++i];
    }

    if (!sConfigMgr->LoadInitial(configFile))
    {
        printf("Invalid or missing configuration file : %s\n", configFile);
        printf("Verify that the file exists and has '[clustergateway]' at the top.\n");
        return 1;
    }

    SF_LOG_INFO("server.clustergateway", "clustergateway revision: %s", SKYFIRE_VER_PRODUCTVERSION_STR);
    SF_LOG_INFO("server.clustergateway", "Using configuration file %s.", configFile);

    uint32 confVersion = sConfigMgr->GetIntDefault("ConfVersion", 0);
    if (confVersion < SKYFIREGATEWAY_CONFIG_VERSION)
        SF_LOG_INFO("server.clustergateway", "WARNING: clustergateway.conf is older than SKYFIREGATEWAY_CONFIG_VERSION.");

    SF_LOG_INFO("server.clustergateway", "%s (Library: %s)", OPENSSL_VERSION_TEXT, SSLeay_version(SSLEAY_VERSION));
    if (!Skyfire::LoadOpenSSLProviders("server.clustergateway"))
        return 1;

    if (!StartDB())
        return 1;

    int32 port = sConfigMgr->GetIntDefault("WorldServerPort", 8085);
    if (port < 1 || port > 0xFFFF)
    {
        SF_LOG_ERROR("server.clustergateway", "WorldServerPort out of range.");
        StopDB();
        return 1;
    }

    std::string bindIp = sConfigMgr->GetStringDefault("BindIP", "0.0.0.0");
    GatewayAcceptor acceptor;
    if (!acceptor.Open(uint16(port), bindIp))
    {
        SF_LOG_ERROR("server.clustergateway", "Cannot bind client listener to %s:%d", bindIp.c_str(), port);
        StopDB();
        return 1;
    }

    SF_LOG_INFO("server.clustergateway", "Listening for 5.4.8 clients on %s:%d", bindIp.c_str(), port);

    ClusterAdminHttp adminHttp;
    int32 adminPort = sConfigMgr->GetIntDefault("Cluster.AdminPort", 8090);
    if (adminPort > 0 && adminPort <= 0xFFFF)
    {
        std::string adminBind = sConfigMgr->GetStringDefault("Cluster.AdminBindIP", "0.0.0.0");
        if (!adminHttp.Open(uint16(adminPort), adminBind))
            SF_LOG_ERROR("server.clustergateway", "Cannot bind admin HTTP to %s:%d", adminBind.c_str(), adminPort);
    }

    std::signal(SIGINT, GatewaySignalHandler);
    std::signal(SIGTERM, GatewaySignalHandler);

    uint32 numLoops = (sConfigMgr->GetIntDefault("MaxPingTime", 30) * (MINUTE * 1000000 / 100000));
    uint32 loopCounter = 0;
    while (!stopEvent)
    {
        Skyfire::SleepForMilliseconds(100);
        if ((++loopCounter) == numLoops)
        {
            loopCounter = 0;
            LoginDatabase.KeepAlive();
        }
    }

    acceptor.Close();
    StopDB();
    SF_LOG_INFO("server.clustergateway", "Halting process...");
    return 0;
}

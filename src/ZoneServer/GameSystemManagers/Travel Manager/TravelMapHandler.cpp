/*
---------------------------------------------------------------------------------------
This source file is part of SWG:ANH (Star Wars Galaxies - A New Hope - Server Emulator)

For more information, visit http://www.swganh.com

Copyright (c) 2006 - 2014 The SWG:ANH Team
---------------------------------------------------------------------------------------
Use of this source code is governed by the GPL v3 license that can be found
in the COPYING file or at http://www.gnu.org/licenses/gpl-3.0.html

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
---------------------------------------------------------------------------------------
*/

#include "TravelMapHandler.h"


#include "anh/logger.h"

#include "Zoneserver/Objects/Inventory.h"
#include "ZoneServer/Objects/Object/ObjectFactory.h"
#include "ZoneServer/Objects/Player Object/PlayerObject.h"
#include "Zoneserver/Objects/Shuttle.h"
#include "TicketCollector.h"
#include "TravelTerminal.h"
#include "TravelTicket.h"
#include "ZoneServer/GameSystemManagers/UI Manager/UIManager.h"
#include "ZoneServer/GameSystemManagers/UI Manager/UITicketSelectListBox.h"
#include "ZoneServer/WorldManager.h"
#include "ZoneServer/GameSystemManagers/Container Manager/ContainerManager.h"
#include "ZoneServer/ZoneOpcodes.h"

#include "ZoneServer\Services\equipment\equipment_service.h"

#include "MessageLib/MessageLib.h"


#include "NetworkManager/DispatchClient.h"
#include "NetworkManager/Message.h"
#include "NetworkManager/MessageDispatch.h"
#include "NetworkManager/MessageFactory.h"
#include "NetworkManager/MessageOpcodes.h"

#include "DatabaseManager/Database.h"
#include "DatabaseManager/DatabaseResult.h"
#include "DatabaseManager/DataBinding.h"

#include "Utils/utils.h"
#include "anh/Utils/rand.h"

bool				TravelMapHandler::mInsFlag    = false;
TravelMapHandler*	TravelMapHandler::mSingleton  = NULL;

//======================================================================================================================

TravelMapHandler::TravelMapHandler(swganh::database::Database* database, MessageDispatch* dispatch,uint32 zoneId)
    : mDBAsyncPool(sizeof(TravelMapAsyncContainer))
    , mDatabase(database)
    , mMessageDispatch(dispatch)
    , mPointCount(0)
    , mRouteCount(0)
    , mZoneId(zoneId)
    , mCellPointsLoaded(false)
    , mRoutesLoaded(false)
    , mWorldPointsLoaded(false)

{
    mMessageDispatch->RegisterMessageCallback(opPlanetTravelPointListRequest,std::bind(&TravelMapHandler::_processTravelPointListRequest, this, std::placeholders::_1, std::placeholders::_2));
    mMessageDispatch->RegisterMessageCallback(opTutorialServerStatusReply, std::bind(&TravelMapHandler::_processTutorialTravelList, this, std::placeholders::_1, std::placeholders::_2));

    // load our points in world
    mDatabase->executeSqlAsync(this,new(mDBAsyncPool.malloc()) TravelMapAsyncContainer(TMQuery_PointsInWorld),
                               "SELECT DISTINCT(terminals.dataStr),terminals.x,terminals.y,terminals.z,terminals.dataInt1,"
                               "terminals.dataInt2,terminals.planet_id,"
                               "spawn_shuttle.X,spawn_shuttle.Y,spawn_shuttle.Z"
                               " FROM %s.terminals"
                               " INNER JOIN %s.spawn_shuttle ON (terminals.dataInt3 = spawn_shuttle.id)"
                               " WHERE terminals.terminal_type = 16 AND"
                               " terminals.parent_id = 0"
                               " GROUP BY terminals.dataStr",
                               mDatabase->galaxy(),mDatabase->galaxy());
   

    // load travel points in cells
    mDatabase->executeSqlAsync(this,new(mDBAsyncPool.malloc()) TravelMapAsyncContainer(TMQuery_PointsInCells),
                               "SELECT DISTINCT(terminals.dataStr),terminals.planet_id,terminals.dataInt1,terminals.dataInt2,"
                               "buildings.x,buildings.y,buildings.z,spawn_shuttle.X,spawn_shuttle.Y,spawn_shuttle.Z"
                               " FROM %s.terminals"
                               " INNER JOIN %s.spawn_shuttle ON (terminals.dataInt3 = spawn_shuttle.id)"
                               " INNER JOIN %s.cells ON (terminals.parent_id = cells.id)"
                               " INNER JOIN %s.buildings ON (cells.parent_id = buildings.id)"
                               " WHERE"
                               " (terminals.terminal_type = 16) AND"
                               " (terminals.parent_id <> 0)"
                               " GROUP BY terminals.dataStr",
                               mDatabase->galaxy(),mDatabase->galaxy(),mDatabase->galaxy(),mDatabase->galaxy());
   
    // load planet routes and base prices
    mDatabase->executeSqlAsync(this,new(mDBAsyncPool.malloc()) TravelMapAsyncContainer(TMQuery_PlanetRoutes),"SELECT * FROM %s.travel_planet_routes",mDatabase->galaxy());
   
}


//======================================================================================================================

TravelMapHandler::~TravelMapHandler()
{
    mInsFlag = false;
    delete(mSingleton);
}
//======================================================================================================================

TravelMapHandler*	TravelMapHandler::Init(swganh::database::Database* database, MessageDispatch* dispatch,uint32 zoneId)
{
    if(!mInsFlag)
    {
        mSingleton = new TravelMapHandler(database,dispatch,zoneId);
        mInsFlag = true;
        return mSingleton;
    }
    else
        return mSingleton;
}

//======================================================================================================================

void TravelMapHandler::Shutdown()
{
    mMessageDispatch->UnregisterMessageCallback(opPlanetTravelPointListRequest);

    for(uint8 i = 0; i < 50; i++)
    {
        TravelPointList::iterator it = mTravelPoints[i].begin();
        while(it != mTravelPoints[i].end())
        {
            delete(*it);
            mTravelPoints[i].erase(it);
            it = mTravelPoints[i].begin();
        }
    }
}

//=======================================================================================================================
void TravelMapHandler::handleDatabaseJobComplete(void* ref,swganh::database::DatabaseResult* result)
{
    TravelMapAsyncContainer* asynContainer = reinterpret_cast<TravelMapAsyncContainer*>(ref);
    switch(asynContainer->mQueryType)
    {
    case TMQuery_PointsInWorld:
    {
        swganh::database::DataBinding* binding = mDatabase->createDataBinding(10);
        binding->addField(swganh::database::DFT_string,offsetof(TravelPoint,descriptor),64,0);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,x),4,1);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,y),4,2);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,z),4,3);
        binding->addField(swganh::database::DFT_uint8,offsetof(TravelPoint,portType),1,4);
        binding->addField(swganh::database::DFT_uint32,offsetof(TravelPoint,taxes),4,5);
        binding->addField(swganh::database::DFT_uint16,offsetof(TravelPoint,planetId),2,6);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,spawnX),4,7);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,spawnY),4,8);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,spawnZ),4,9);

        uint64 count = result->getRowCount();

        for(uint64 i = 0; i < count; i++)
        {
            TravelPoint* travelPoint = new TravelPoint();
            result->getNextRow(binding,travelPoint);

            mTravelPoints[travelPoint->planetId].push_back(travelPoint);
        }

        LOG(info) << "Loaded " << count << " outdoor travel points";

        mPointCount += static_cast<uint32>(count);
        mWorldPointsLoaded = true;

        mDatabase->destroyDataBinding(binding);
    }
    break;

    case TMQuery_PointsInCells:
    {
        swganh::database::DataBinding* binding = mDatabase->createDataBinding(10);
        binding->addField(swganh::database::DFT_string,offsetof(TravelPoint,descriptor),64,0);
        binding->addField(swganh::database::DFT_uint16,offsetof(TravelPoint,planetId),2,1);
        binding->addField(swganh::database::DFT_uint8,offsetof(TravelPoint,portType),1,2);
        binding->addField(swganh::database::DFT_uint32,offsetof(TravelPoint,taxes),4,3);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,x),4,4);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,y),4,5);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,z),4,6);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,spawnX),4,7);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,spawnY),4,8);
        binding->addField(swganh::database::DFT_float,offsetof(TravelPoint,spawnZ),4,9);

        uint64 count = result->getRowCount();

        for(uint64 i = 0; i < count; i++)
        {
            TravelPoint* travelPoint = new TravelPoint();
            result->getNextRow(binding,travelPoint);

            mTravelPoints[travelPoint->planetId].push_back(travelPoint);
        }

        LOG(info)  << "Loaded " << count << " in-cell travel points";

        mPointCount += static_cast<uint32>(count);
        mCellPointsLoaded = true;

        mDatabase->destroyDataBinding(binding);
    }
    break;

    case TMQuery_PlanetRoutes:
    {

        TravelRoute route;

        swganh::database::DataBinding* binding = mDatabase->createDataBinding(3);
        binding->addField(swganh::database::DFT_uint16,offsetof(TravelRoute,srcId),2,0);
        binding->addField(swganh::database::DFT_uint16,offsetof(TravelRoute,destId),2,1);
        binding->addField(swganh::database::DFT_int32,offsetof(TravelRoute,price),4,2);

        uint64 count = result->getRowCount();

        for(uint64 i = 0; i < count; i++)
        {
            result->getNextRow(binding,&route);
            mTravelRoutes[route.srcId].push_back(std::make_pair(route.destId,route.price));
        }

        LOG(info) << "Loaded " << count << " routes";

        mRouteCount = static_cast<uint32>(count);
        mRoutesLoaded = true;

        mDatabase->destroyDataBinding(binding);
    }
    break;

    default:
        break;
    }

    mDBAsyncPool.free(asynContainer);

    if(mWorldPointsLoaded && mCellPointsLoaded && mRoutesLoaded)
    {
        mWorldPointsLoaded = false;
        mCellPointsLoaded = false;
        mRoutesLoaded = false;
    }
}

void TravelMapHandler::_processTutorialTravelList(Message* message, DispatchClient* client)
{
    PlayerObject* player = dynamic_cast<PlayerObject*>(gWorldManager->getObjectById(message->getUint64()));


    //Why do we do this? Because I spent 2 days trying to get it work to discover it was call order.
    uint8 tatooine	= message->getUint8();
    uint8 corellia	= message->getUint8();
    uint8 talus		= message->getUint8();
    uint8 rori		= message->getUint8();
    uint8 naboo		= message->getUint8();

    if(player)
    {
        gMessageLib->sendStartingLocationList(
            player,
            tatooine,	//Tatooine
            corellia,	//Corellia
            talus,		//Talus
            rori,		//Rori
            naboo		//Naboo
        );
    }
}

//=======================================================================================================================

void TravelMapHandler::_processTravelPointListRequest(Message* message,DispatchClient* client)
{
    PlayerObject* playerObject = dynamic_cast<PlayerObject*>(gWorldManager->getObjectById(message->getUint64()));

    if(playerObject != NULL && playerObject->isConnected())
    {
        // we need to know where we query from
        TravelTerminal* terminal = playerObject->getTravelPoint();

        if(terminal == NULL)
        {
            DLOG(info) << "TravelMapHandler::_processTravelListRequest: No TravelPosition set, player "<<playerObject->getId();
            return;
        }

        std::string requestedPlanet = message->getStringAnsi();

        // find our planetId
        uint8 planetId = gWorldManager->getPlanetIdByName(requestedPlanet);

        char	queryPoint[64];
        TravelPoint* qP = NULL;

        // get our query point
        strcpy(queryPoint,(playerObject->getTravelPoint())->getPosDescriptor().getAnsi());

        TravelPointList::iterator it = mTravelPoints[mZoneId].begin();
        TravelPointList::iterator end = mTravelPoints[mZoneId].end();
        while(it != end)
        {
            TravelPoint* tp = (*it);

            if(strcmp(queryPoint,tp->descriptor) == 0)
            {
                qP = tp;
                break;
            }
            ++it;
        }

        TravelPointList printListing;
        it = mTravelPoints[planetId].begin();
        end = mTravelPoints[planetId].end();

        while(it != end)
        {
            // If the requested planet list is not the planet of the current zone
            // then only list it if the origin is a starport and the destination is a starport.
            if((mZoneId != planetId && qP->portType == 1 && (*it)->portType == 1) ||
                    mZoneId == planetId) // Show all starports/shuttleports on this planet.
            {
                printListing.push_back((*it));
            }
            ++it;
        }

        //Build our message.
        gMessageFactory->StartMessage();
        gMessageFactory->addUint32(opPlanetTravelPointListResponse);
        gMessageFactory->addString(requestedPlanet);

        end = printListing.end();
        gMessageFactory->addUint32(printListing.size());
        for(it = printListing.begin(); it != end; ++it)
        {
            gMessageFactory->addString((*it)->descriptor);
        }

        gMessageFactory->addUint32(printListing.size());
        for(it = printListing.begin(); it != end; ++it)
        {
            gMessageFactory->addFloat((*it)->x);
            gMessageFactory->addFloat((*it)->y);
            gMessageFactory->addFloat((*it)->z);
        }

        gMessageFactory->addUint32(printListing.size());
        for(it = printListing.begin(); it != end; ++it)
        {
            gMessageFactory->addUint32((*it)->taxes);
        }

        gMessageFactory->addUint32(printListing.size());
        for(it = printListing.begin(); it != end; ++it)
        {
            // If it's a starport send a 1, otherwise shuttleports are set to 0
            if ((*it)->portType == portType_Starport) {
                gMessageFactory->addUint8(1);
            } else {
                gMessageFactory->addUint8(0);
            }
        }

        playerObject->getClient()->SendChannelA(gMessageFactory->EndMessage(), playerObject->getAccountId(), CR_Client, 5);
    }
    else
        DLOG(info) << "TravelMapHandler::_processTravelListRequest: Couldnt find player for " << client->getAccountId();
}

//=======================================================================================================================

void TravelMapHandler::getTicketInformation(BStringVector vQuery,TicketProperties* ticketProperties)
{
    ticketProperties->srcPlanetId = 0;
    while((strcmp(vQuery[0].getAnsi(),gWorldManager->getPlanetNameById(static_cast<uint8>(ticketProperties->srcPlanetId))) != 0))
        ticketProperties->srcPlanetId++;

    ticketProperties->dstPlanetId = 0;
    while((strcmp(vQuery[2].getAnsi(),gWorldManager->getPlanetNameById(static_cast<uint8>(ticketProperties->dstPlanetId))) != 0))
        ticketProperties->dstPlanetId++;

    TravelRoutes::iterator it = mTravelRoutes[ticketProperties->srcPlanetId].begin();
    while(it != mTravelRoutes[ticketProperties->srcPlanetId].end())
    {
        if((*it).first == ticketProperties->dstPlanetId)
        {
            ticketProperties->price = (*it).second;
            break;
        }
        ++it;
    }

    TravelPointList::iterator tpIt = mTravelPoints[ticketProperties->srcPlanetId].begin();
    while(tpIt != mTravelPoints[ticketProperties->srcPlanetId].end())
    {
        TravelPoint* tp = (*tpIt);
        BString desc = tp->descriptor;

        if(strcmp(strRep(std::string(vQuery[1].getAnsi()),"_"," ").c_str(),desc.getAnsi()) == 0)
        {
            ticketProperties->srcPoint = tp;
            break;
        }

        ++tpIt;
    }

    ticketProperties->dstPoint = NULL;

    tpIt = mTravelPoints[ticketProperties->dstPlanetId].begin();
    while(tpIt != mTravelPoints[ticketProperties->dstPlanetId].end())
    {
        //tp = (*tpIt);

        BString desc = (*tpIt)->descriptor;

        if(strcmp(strRep(std::string(vQuery[3].getAnsi()),"_"," ").c_str(),desc.getAnsi()) == 0)
        {
            ticketProperties->dstPoint = (*tpIt);
            ticketProperties->price += (*tpIt)->taxes;

            break;
        }
        ++tpIt;
    }
}

//=======================================================================================================================

TravelPoint* TravelMapHandler::getTravelPoint(uint16 planetId, std::string name)
{
    TravelPointList::iterator it = mTravelPoints[planetId].begin();
    while(it != mTravelPoints[planetId].end())
    {
        if(strcmp(name.c_str(),(*it)->descriptor) == 0)
            return(*it);

        ++it;
    }
    return(NULL);
}

//=======================================================================================================================

bool TravelMapHandler::findTicket(PlayerObject* player, BString port)
{
    uint32	zoneId = gWorldManager->getZoneId();

	auto inventory = gWorldManager->getKernel()->GetServiceManager()->GetService<swganh::equipment::EquipmentService>("EquipmentService")->GetEquippedObject(player, "inventory");
	bool found = false;
	inventory->ViewObjects(player, 0, true, [&] (Object* object) {

        TravelTicket* ticket = dynamic_cast<TravelTicket*>(object);
        if(ticket)
        {
            BString srcPoint		= (int8*)((ticket->getAttribute<std::string>("travel_departure_point")).c_str());
            uint16 srcPlanetId	= static_cast<uint16>(gWorldManager->getPlanetIdByName(ticket->getAttribute<std::string>("travel_departure_planet")));

            // see if we got at least 1
            if(srcPlanetId == zoneId && strcmp(srcPoint.getAnsi(),port.getAnsi()) == 0)
            {
                found = false;
            }
        }
    });
    return found;
}

//=======================================================================================================================

void TravelMapHandler::createTicketSelectMenu(PlayerObject* player, Shuttle* shuttle, BString port)
{
    StringVector	availableTickets;
    uint32			zoneId = gWorldManager->getZoneId();

	auto inventory = gWorldManager->getKernel()->GetServiceManager()->GetService<swganh::equipment::EquipmentService>("EquipmentService")->GetEquippedObject(player, "inventory");
	inventory->ViewObjects(player, 0, true, [&] (Object* object) {
    
        TravelTicket* ticket = dynamic_cast<TravelTicket*>(object);
        if(ticket)
        {
            BString srcPoint		= (int8*)((ticket->getAttribute<std::string>("travel_departure_point")).c_str());
            uint16 srcPlanetId	= static_cast<uint8>(gWorldManager->getPlanetIdByName(ticket->getAttribute<std::string>("travel_departure_planet")));

            if(srcPlanetId == zoneId && strcmp(srcPoint.getAnsi(),port.getAnsi()) == 0)
            {
                BString dstPoint = (int8*)((ticket->getAttribute<std::string>("travel_arrival_point")).c_str());

                availableTickets.push_back(dstPoint.getAnsi());
            }
        }
    });

    gUIManager->createNewTicketSelectListBox(this,"handleTicketSelect","select destination","Select destination",availableTickets, player, port, shuttle);
}

//=======================================================================================================================

void TravelMapHandler::handleUIEvent(uint32 action,int32 element,std::u16string inputStr,UIWindow* window, std::shared_ptr<WindowAsyncContainerCommand> AsyncContainer)
{
    if(!action && element != -1 )
    {
        uint32					zoneId			= gWorldManager->getZoneId();
        PlayerObject*			player			= window->getOwner();
        UITicketSelectListBox*	listBox			= dynamic_cast<UITicketSelectListBox*>(window);

        if(player->getSurveyState() || player->getSamplingState() || !listBox)
            return;

        Shuttle* shuttle = listBox->getShuttle();

        if(!shuttle)
        {
            return;
        }

        if (!shuttle->availableInPort())
        {
            gMessageLib->SendSystemMessage(::common::OutOfBand("travel", "shuttle_not_available"), player);
            return;
        }

        if((player->getParentId() != shuttle->getParentId()) || (glm::distance(player->mPosition, shuttle->mPosition) > 25.0f))
        {
            gMessageLib->SendSystemMessage(::common::OutOfBand("travel", "boarding_too_far"), player);

            return;
        }

        auto inventory = gWorldManager->getKernel()->GetServiceManager()->GetService<swganh::equipment::EquipmentService>("EquipmentService")->GetEquippedObject(player, "inventory");
		inventory->ViewObjects(player, 0, true, [&] (Object* object) {
            TravelTicket* ticket = dynamic_cast<TravelTicket*>(object);
            if(ticket)
            {
                BString srcPoint		= (int8*)((ticket->getAttribute<std::string>("travel_departure_point")).c_str());
                std::string dstPointStr	= ticket->getAttribute<std::string>("travel_arrival_point");
                uint16 srcPlanetId	= static_cast<uint8>(gWorldManager->getPlanetIdByName(ticket->getAttribute<std::string>("travel_departure_planet")));
                uint16 dstPlanetId	= static_cast<uint8>(gWorldManager->getPlanetIdByName(ticket->getAttribute<std::string>("travel_arrival_planet")));

                StringVector* items = (dynamic_cast<UIListBox*>(window))->getDataItems();
                std::string selectedDst = items->at(element);

				if(srcPlanetId == zoneId && (strcmp(dstPointStr.c_str(),selectedDst.c_str()) == 0)&&(strcmp(srcPoint.getAnsi(),listBox->getPort().getAnsi()) == 0))
                {
                    TravelPoint* dstPoint = getTravelPoint(dstPlanetId,dstPointStr);

                    if(dstPoint != NULL)
                    {
                        glm::vec3 destination;
                        destination.x = dstPoint->spawnX + (gRandom->getRand()%5 - 2);
                        destination.y = dstPoint->spawnY;
                        destination.z = dstPoint->spawnZ + (gRandom->getRand()%5 - 2);

                        // If it's on this planet, then just warp, otherwize zone
                        if(dstPlanetId == zoneId)
                        {
                            // only delete the ticket if we are warping on this planet.
							TangibleObject* tO = dynamic_cast<TangibleObject*>(gWorldManager->getObjectById(ticket->getParentId()));
							gContainerManager->deleteObject(ticket, tO);

							gWorldManager->warpPlanet(player,destination,0);
                        }
                        else
                        {
                            gMessageLib->sendClusterZoneTransferRequestByTicket(player,ticket->getId(), dstPoint->planetId);
                        }
                    }
                    return;
                }
            }
          
        });
    }
}

//=======================================================================================================================

void TravelMapHandler::useTicket(PlayerObject* player, TravelTicket* ticket,Shuttle* shuttle)
{
    uint32	zoneId = gWorldManager->getZoneId();

    // in range check
    if(player->getParentId() !=  shuttle->getParentId())
    {
        gMessageLib->SendSystemMessage(::common::OutOfBand("travel", "shuttle_not_available"), player);
        return;
    }

    TicketCollector* collector = dynamic_cast<TicketCollector*>(gWorldManager->getObjectById(shuttle->getCollectorId()));
    BString port = collector->getPortDescriptor();

	//hardcoded shit - but in theed theres always a shuttle
    if(port.getCrc() == BString("Theed Starport").getCrc())
    {
        shuttle->setShuttleState(ShuttleState_InPort);
    }

    if (!shuttle->availableInPort())
    {
        gMessageLib->SendSystemMessage(::common::OutOfBand("travel", "shuttle_not_available"), player);
        return;
    }

    BString srcPoint		= (int8*)((ticket->getAttribute<std::string>("travel_departure_point")).c_str());
    uint16 srcPlanetId	= static_cast<uint8>(gWorldManager->getPlanetIdByName(ticket->getAttribute<std::string>("travel_departure_planet")));
    uint16 dstPlanetId	= static_cast<uint8>(gWorldManager->getPlanetIdByName(ticket->getAttribute<std::string>("travel_arrival_planet")));
    std::string dstPointStr	= ticket->getAttribute<std::string>("travel_arrival_point");

    // see if we are at the right location
    if(srcPlanetId != zoneId || strcmp(srcPoint.getAnsi(),port.getAnsi()) != 0)    {
        gMessageLib->SendSystemMessage(::common::OutOfBand("travel", "wrong_shuttle"), player);
        return;
    }

    //ok lets travel
    if(TravelPoint* dstPoint = gTravelMapHandler->getTravelPoint(dstPlanetId,dstPointStr))
    {
        glm::vec3 destination;
        destination.x = dstPoint->spawnX + (gRandom->getRand()%5 - 2);
        destination.y = dstPoint->spawnY;
        destination.z = dstPoint->spawnZ + (gRandom->getRand()%5 - 2);

        // If it's on this planet, then just warp, otherwize zone
        if(dstPlanetId == zoneId)
        {
            // only delete the ticket if we are warping on this planet.
			// delete the ticket for all watchers
	
            TangibleObject* tO = dynamic_cast<TangibleObject*>(gWorldManager->getObjectById(ticket->getParentId()));
            gContainerManager->deleteObject(ticket, tO);

            ticket = NULL;
            gWorldManager->warpPlanet(player,destination,0);
        }
        else
        {
            gMessageLib->sendClusterZoneTransferRequestByTicket(player,ticket->getId(), dstPoint->planetId);
        }
        return;
    }
    else
    {
    }
}

//=======================================================================================================================




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

#include "InventoryFactory.h"

#include "anh/logger.h"

#include "Zoneserver/Objects/Inventory.h"
#include "ZoneServer/Objects/Object/ObjectFactoryCallback.h"
#include "ZoneServer/Objects/Tangible Object/TangibleFactory.h"
#include "ZoneServer\Objects\Object\ObjectManager.h"
#include "ZoneServer\PlayerEnums.h"
#include "ZoneServer/WorldManager.h"
#include "DatabaseManager/Database.h"
#include "DatabaseManager/DatabaseResult.h"
#include "DatabaseManager/DataBinding.h"

#include "Utils/utils.h"

#include <cassert>

//=============================================================================

bool					InventoryFactory::mInsFlag    = false;
InventoryFactory*		InventoryFactory::mSingleton  = NULL;

//======================================================================================================================

InventoryFactory*	InventoryFactory::Init(swganh::app::SwganhKernel*	kernel)
{
    if(!mInsFlag)
    {
        mSingleton = new InventoryFactory(kernel);
        mInsFlag = true;
        return mSingleton;
    }
    else
        return mSingleton;
}

//=============================================================================

InventoryFactory::InventoryFactory(swganh::app::SwganhKernel*	kernel) : FactoryBase(kernel)
{
    mTangibleFactory = TangibleFactory::Init(kernel);

    _setupDatabindings();
}

//=============================================================================

InventoryFactory::~InventoryFactory()
{
    _destroyDatabindings();

    mInsFlag = false;
    delete(mSingleton);
}

//=============================================================================

void InventoryFactory::handleDatabaseJobComplete(void* ref,swganh::database::DatabaseResult* result)
{
    QueryContainerBase* asyncContainer = reinterpret_cast<QueryContainerBase*>(ref);

    switch(asyncContainer->mQueryType)
    {
    case IFQuery_MainInventoryData:
    {
        Inventory* inventory = _createInventory(result);

        QueryContainerBase* asContainer = new(mQueryContainerPool.ordered_malloc()) QueryContainerBase(asyncContainer->mOfCallback,IFQuery_ObjectCount,asyncContainer->mClient);
        asContainer->mObject = inventory;

		std::stringstream sql;
		sql << "SELECT " << mDatabase->galaxy() << ".sf_getInventoryObjectCount(" << inventory->getId() << ");";
        mDatabase->executeSqlAsync(this,asContainer, sql.str());
        
    }
    break;

    case IFQuery_ObjectCount:
    {
        Inventory* inventory = dynamic_cast<Inventory*>(asyncContainer->mObject);

        uint32 objectCount;
        swganh::database::DataBinding* binding = mDatabase->createDataBinding(1);

        binding->addField(swganh::database::DFT_uint32,0,4);
        result->getNextRow(binding,&objectCount);

        inventory->setObjectLoadCounter(objectCount);

        if(objectCount != 0)
        {
            uint64 invId = inventory->getId();

            inventory->setLoadState(LoadState_Loading);

            // query contents
            QueryContainerBase* asContainer = new(mQueryContainerPool.ordered_malloc()) QueryContainerBase(asyncContainer->mOfCallback,IFQuery_Objects,asyncContainer->mClient);
            asContainer->mObject = inventory;

			std::stringstream sql;

			//"(SELECT \'containers\',containers.id FROM %s.containers INNER JOIN %s.container_types ON (containers.container_type = container_types.id)"
			//" WHERE (container_types.name NOT LIKE 'unknown') AND (containers.parent_id = %"PRIu64"))"
			sql << "(SELECT \'items\',items.id FROM "
				<< mDatabase->galaxy() << ".items WHERE (parent_id=" << invId << "))" 
				<< "UNION (SELECT \'resource_containers\',resource_containers.id FROM "
				<< mDatabase->galaxy() << ".resource_containers WHERE (parent_id="
				<< invId << "))";

            //why would we load the lootcontainers and trashpiles for the inventory ???
            //containers are normal items like furniture, lightsabers and stuff
            mDatabase->executeSqlAsync(this,asContainer, sql.str());
                
           
        }
        else
        {
            inventory->setLoadState(LoadState_Loaded);
            asyncContainer->mOfCallback->handleObjectReady(inventory,asyncContainer->mClient);
        }

        mDatabase->destroyDataBinding(binding);
    }
    break;

    case IFQuery_Objects:
    {
        Inventory* inventory = dynamic_cast<Inventory*>(asyncContainer->mObject);

        Type1_QueryContainer queryContainer;

        swganh::database::DataBinding*	binding = mDatabase->createDataBinding(2);
        binding->addField(swganh::database::DFT_bstring,offsetof(Type1_QueryContainer,mString),64,0);
        binding->addField(swganh::database::DFT_uint64,offsetof(Type1_QueryContainer,mId),8,1);

        uint64 count = result->getRowCount();

        mObjectLoadMap.insert(std::make_pair(inventory->getId(),new(mILCPool.ordered_malloc()) InLoadingContainer(inventory,asyncContainer->mOfCallback,asyncContainer->mClient,static_cast<uint8>(count))));

        for(uint32 i = 0; i < count; i++)
        {
            result->getNextRow(binding,&queryContainer);

            
            if(strcmp(queryContainer.mString.getAnsi(),"items") == 0)
                mTangibleFactory->requestObject(this,queryContainer.mId,TanGroup_Item,0,asyncContainer->mClient);
            else if(strcmp(queryContainer.mString.getAnsi(),"resource_containers") == 0)
                mTangibleFactory->requestObject(this,queryContainer.mId,TanGroup_ResourceContainer,0,asyncContainer->mClient);
        }

        mDatabase->destroyDataBinding(binding);
    }
    break;

    default:
        break;
    }

    mQueryContainerPool.free(asyncContainer);
}

//=============================================================================

void InventoryFactory::requestObject(ObjectFactoryCallback* ofCallback,uint64 id,uint16 subGroup,uint16 subType,DispatchClient* client)
{

	std::stringstream sql;
	sql << "SELECT inventories.id,inventories.credits,inventory_types.object_string,inventory_types.name,inventory_types.file,"
		<< "inventory_types.slots FROM " << mDatabase->galaxy() << ".inventories INNER JOIN " << mDatabase->galaxy() << ".inventory_types ON (inventories.inventory_type = inventory_types.id)"
		<< " WHERE (inventories.id = " << id << ")";

    mDatabase->executeSqlAsync(this,new(mQueryContainerPool.ordered_malloc()) QueryContainerBase(ofCallback,IFQuery_MainInventoryData,client),sql.str());
   
}

//=============================================================================

Inventory* InventoryFactory::_createInventory(swganh::database::DatabaseResult* result)
{
    if (!result->getRowCount()) {
    	return nullptr;
    }

    Inventory*	inventory = new Inventory();

    // get our results
    result->getNextRow(mInventoryBinding,(void*)inventory);
	
	gObjectManager->LoadSlotsForObject(inventory);
    
	//thats somewhat a hack
	inventory->setParentId(inventory->mId - INVENTORY_OFFSET);

    inventory->setCapacity(inventory->mMaxSlots);
    gWorldManager->addObject(inventory,true);

	return inventory;
}

//=============================================================================

void InventoryFactory::_setupDatabindings()
{
    // inventory binding
    mInventoryBinding = mDatabase->createDataBinding(6);
    mInventoryBinding->addField(swganh::database::DFT_uint64,offsetof(Inventory,mId),8,0);
    mInventoryBinding->addField(swganh::database::DFT_int32,offsetof(Inventory,mCredits),4,1);
	mInventoryBinding->addField(swganh::database::DFT_stdstring,offsetof(Inventory,template_string_),256,2);
    mInventoryBinding->addField(swganh::database::DFT_bstring,offsetof(Inventory,mName),64,3);
    mInventoryBinding->addField(swganh::database::DFT_bstring,offsetof(Inventory,mNameFile),64,4);
    mInventoryBinding->addField(swganh::database::DFT_uint8,offsetof(Inventory,mMaxSlots),1,5);
}

//=============================================================================

void InventoryFactory::_destroyDatabindings()
{
    mDatabase->destroyDataBinding(mInventoryBinding);
}

//=============================================================================

void InventoryFactory::handleObjectReady(Object* object,DispatchClient* client)
{
    //we either have the ID of the inventory or of the player
    //the inventory of course is stored under its own Id

    InLoadingContainer* ilc	= _getObject(object->getParentId());


    if (! ilc) {
    	LOG(warning) << "Could not locate InLoadingContainer for object with id [" << object->getId() << "]";
        assert(ilc && "InventoryFactory::handleObjectReady unable to find InLoadingContainer");//moved below the return
        return;
    }

    Inventory*			inventory	= dynamic_cast<Inventory*>(ilc->mObject);

    gWorldManager->addObject(object,true);

    //for unequipped items only
	inventory->InitializeObject(object);
	LOG(info) << "InventoryFactory::handleObjectReady : " << object->GetTemplate();

	//LOG(info) << "InventoryFactory::handleObjectReady -> to load : " << inventory->getObjectLoadCounter()  << " loaded : " << inventory->getHeadCount();
    if(inventory->getObjectLoadCounter() == (inventory->getHeadCount()))
    {
		LOG(info) << "InventoryFactory::handleObjectReady DONE ";
        inventory->setLoadState(LoadState_Loaded);

        if(!(_removeFromObjectLoadMap(inventory->getId())))
        	LOG(info) << "Failed removing object from loadmap";

        ilc->mOfCallback->handleObjectReady(inventory,ilc->mClient);

        mILCPool.free(ilc);
    }
}

//=============================================================================


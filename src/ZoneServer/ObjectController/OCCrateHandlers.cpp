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
//#include "ZoneServer/Objects/BankTerminal.h"
//#include "ZoneServer/Objects/CraftingTool.h"
//#include "ZoneServer/GameSystemManagers/Resource Manager/CurrentResource.h"
#include "Zoneserver/Objects/Inventory.h"
#include "Zoneserver/Objects/Item.h"
//#include "ZoneServer/GameSystemManagers/NPC Manager/NPCObject.h"
#include "Zoneserver/ObjectController/ObjectController.h"
#include "ZoneServer/ObjectController/ObjectControllerOpcodes.h"
#include "ZoneServer/ObjectController/ObjectControllerCommandMap.h"
#include "ZoneServer/Objects/Object/ObjectFactory.h"
#include "ZoneServer/Objects/Player Object/PlayerObject.h"
#include "ZoneServer/GameSystemManagers/Structure Manager/FactoryCrate.h"
//#include "ZoneServer/GameSystemManagers/UI Manager/UIManager.h"
#include "ZoneServer/WorldConfig.h"
#include "ZoneServer/WorldManager.h"
#include "ZoneServer/GameSystemManagers/Container Manager/ContainerManager.h"

#include "MessageLib/MessageLib.h"
#include "NetworkManager/Message.h"
#include <boost/lexical_cast.hpp>

#include <cassert>

//======================================================================================================================
//splits a factory crate

void	ObjectController::_handleFactoryCrateSplit(uint64 targetId,Message* message,ObjectControllerCmdProperties* cmdProperties)
{

}
//======================================================================================================================
//extracts an item out of a factory crate

void	ObjectController::_ExtractObject(uint64 targetId,Message* message,ObjectControllerCmdProperties* cmdProperties)
{
    PlayerObject*		playerObject		= dynamic_cast<PlayerObject*>(mObject);
    FactoryCrate*		crate				= dynamic_cast<FactoryCrate*>(gWorldManager->getObjectById(targetId));

    if(!crate)
    {
        DLOG(info) << "ObjectController::_ExtractObject: Crate does not exist!";
        return;
    }

    //get the crates containing container  we can use the unified interface thks to virtual functions :)
    TangibleObject* tO = dynamic_cast<TangibleObject* >(gWorldManager->getObjectById(crate->getParentId()));
    if(!tO)
    {
        assert(false && "ObjectController::_ExtractObject inventory must be a tangible object");
        return;
    }

    if(!tO->checkCapacity())
    {
        //check if we can fit an additional item in our inventory
        //TODO: say something
        return;
    }

    //create the new item
    gObjectFactory->requestNewClonedItem(tO,crate->getLinkedObject()->getId(),tO->getId());

    //decrease crate content
    int32 content = crate->decreaseContent(1);
    if(!content)
    {
		TangibleObject* container = dynamic_cast<TangibleObject*>(gWorldManager->getObjectById(crate->getParentId()));
		gContainerManager->deleteObject(crate, container);
        
        return;
    }

    if(content < 0)
    {
        assert(false && "ObjectController::_ExtractObject crate must not have negative content");
        return;
    }

    gMessageLib->sendUpdateCrateContent(crate,playerObject);

    return;


}

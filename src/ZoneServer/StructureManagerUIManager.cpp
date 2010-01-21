/*
---------------------------------------------------------------------------------------
This source file is part of swgANH (Star Wars Galaxies - A New Hope - Server Emulator)
For more information, see http://www.swganh.org


Copyright (c) 2006 - 2010 The swgANH Team

---------------------------------------------------------------------------------------
*/
#include "WorldConfig.h"
#include "StructureManager.h"
#include "HarvesterObject.h"
#include "FactoryObject.h"
#include "ManufacturingSchematic.h"
#include "Inventory.h"
#include "DataPad.h"
#include "Bank.h"
#include "ResourceContainer.h"
#include "ResourceType.h"
#include "ObjectFactory.h"
#include "PlayerObject.h"
#include "PlayerStructure.h"
#include "WorldManager.h"

#include "LogManager/LogManager.h"
#include "UIManager.h"

//#include "Common/DispatchClient.h"


void StructureManager::createNewFactorySchematicBox(PlayerObject* player, FactoryObject* factory)
{

	
	string wText = "Current schematic installed: ";
	
	if(factory->getManSchemID())
	{
		string name = factory->getSchematicCustomName();
		name.convert(BSTRType_ANSI);
		wText << name.getAnsi();	
		
		
		if(!factory->getSchematicCustomName().getLength())
		{
			wText <<"@"<<factory->getSchematicFile().getAnsi()<<":"<<factory->getSchematicName().getAnsi();		
		}
	}


	BStringVector attributesMenu;
	WindowAsyncContainerCommand* asyncContainer = new  WindowAsyncContainerCommand(Window_Query_Add_Schematic);

	//now get all man schematics in the players datapad
	Datapad* datapad								= dynamic_cast<Datapad*>(player->getEquipManager()->getEquippedObject(CreatureEquipSlot_Datapad));
	ManufacturingSchematicList*				mList	= datapad->getManufacturingSchematics();
	ManufacturingSchematicList::iterator	mListIt	= mList->begin();
	
	string lText;

	while(mListIt != mList->end())
	{
		ManufacturingSchematic* man = (*mListIt);
		if(!man)
		{
			gLogger->logMsgF("UIManager::Man Schematic doesnt exist",MSG_NORMAL);
			mListIt++;
			continue;

		}
		
		Item* item = man->getItem();
		if(!item)
		{
			gLogger->logMsgF("UIManager::Man Schematic Item doesnt exist",MSG_NORMAL);
			mListIt++;
			continue;

		}
		
		string name = item->getCustomName();

		
		//string name = factory->getSchematicCustomName();
		name.convert(BSTRType_ANSI);
		lText << name.getAnsi();	
	
		if(!name.getLength())
		{
			lText <<"@"<<item->getNameFile().getAnsi()<<":"<<item->getName().getAnsi();		
		}

		attributesMenu.push_back(lText);
		asyncContainer->SortedList.push_back(man->getId());
		
		mListIt++;
	}
	
	uint8 LBType = SUI_LB_CANCEL_SCHEMATIC_USE;

	if(factory->getManSchemID())
		LBType =  SUI_LB_CANCEL_SCHEMATIC_REMOVEUSE;

	
	asyncContainer->PlayerId		= player->getId();
	
	  
	gUIManager->createNewListBox(factory,"handleUpdateSchematic","@sui:swg", wText, attributesMenu, player, SUI_Window_Factory_Schematics,LBType,factory->getId(),0,asyncContainer);
}

//============================================================================================
// Creates the status box for a structure
//
void StructureManager::createNewStructureStatusBox(PlayerObject* player, PlayerStructure* structure)
{
	//player_structure structure_name_prompt

	string wText = "Structure Name: ";
	string name = structure->getCustomName();
	name.convert(BSTRType_ANSI);
	wText << name.getAnsi();	
	
	
	if(!structure->getCustomName().getLength())
	{
		wText = "@player_structure:structure_name_prompt ";
		wText <<"@"<<structure->getNameFile().getAnsi()<<":"<<structure->getName().getAnsi();		
	}

	BStringVector attributesMenu;

	//Owner
	int8 text[128];
	sprintf(text,"Owner:%s",structure->getOwnersName());
	attributesMenu.push_back(text);


	//private vs public
	if(structure->getPrivate())
	{
		sprintf(text,"This structure is private");
	}
	else
	{
		sprintf(text,"This structure is public");
	}

	attributesMenu.push_back(text);

	// condition
	uint32 currentCondition = structure->getMaxCondition() - structure->getDamage();

	sprintf(text,"Condition: %u%s",(uint32)(currentCondition/(structure->getMaxCondition() /100)),"%");

	attributesMenu.push_back(text);

	
	//Maintenance Pool
	float maint = (float)structure->getCurrentMaintenance();
	float rate  = (float)structure->getMaintenanceRate();
	uint32 hours , days, minutes;
	

	days = (uint32)(maint / (rate *24));
	maint -= days *(rate*24);

	hours = (uint32)(maint / rate);
	maint -= (uint32)(hours *rate);

	minutes = (uint32)(maint/(rate/60));
	
	sprintf(text,"Maintenance Pool: %u(%u days, %u hours, %u minutes)",(uint32)structure->getCurrentMaintenance(),days,hours,minutes);
	attributesMenu.push_back(text);

	//Maintenance rate
	sprintf(text,"Maintenance Rate: %u/hr",(uint32)rate);
	attributesMenu.push_back(text);

	//Power Pool
	uint32 power = structure->getCurrentPower();
	rate = (float)structure->getPowerConsumption();
	
	days = (uint32)(power / (rate *24));
	power -=(uint32)( days *(rate*24));
	
	hours = (uint32)(power / rate);
	power -= (uint32)(hours *rate);
	
	minutes = (uint32)(power/ (rate/60));
	
	sprintf(text,"Power Reserves: %u(%u days, %u hours, %u minutes)",structure->getCurrentPower(),days,hours,minutes);
	attributesMenu.push_back(text);

	//Power Consumption
	sprintf(text,"Power Consumption: %u units/hr",structure->getPowerConsumption());
	attributesMenu.push_back(text);

	

	//answer = x/(total/100);
	//answer = x*(total/100);
	// total = 100%
	
	gUIManager->createNewListBox(structure,"handle Structure Destroy","@player_structure:structure_status_t", wText, attributesMenu, player, SUI_Window_Structure_Status,SUI_LB_CANCELREFRESH,structure->getId());
}



//============================================================================================
// Renames a structure
//
void StructureManager::createRenameStructureBox(PlayerObject* player, PlayerStructure* structure)
{

	string text = "Please enter the new name you would like for this object.";
	
	int8 caption[32];
	sprintf(caption,"NAME THE OBJECT");

	BStringVector vector;


	int8 sName[128];

	string name = structure->getCustomName();			
	name.convert(BSTRType_ANSI);
	sprintf(sName,"%s",name.getAnsi());
	if(!name.getLength())
	{
		sprintf(sName,"@%s:%s",structure->getNameFile().getAnsi(),structure->getName().getAnsi());
		
	}

	vector.push_back(sName);

	gUIManager->createNewInputBox(structure,sName,caption,text.getAnsi(),vector,player,SUI_IB_NODROPDOWN_OKCANCEL,SUI_Window_Structure_Rename,68);
	
}

//============================================================================================
//Transfers power between inventory and harvester
//
void StructureManager::createPowerTransferBox(PlayerObject* player, PlayerStructure* structure)
{

	int8 text[255];
	
	uint32 structurePower = structure->getCurrentPower();
	uint32 playerPower = gStructureManager->getCurrentPower(player);

	sprintf(text,"Select the amount of power you would like to deposit.\xa\xa Current Power Value = %u ",structurePower);
	
	int8 caption[32];
	sprintf(caption,"SELECT AMOUNT");
	int8 sName[128];

	string name = structure->getCustomName();			
	name.convert(BSTRType_ANSI);
	sprintf(sName,"%s",name.getAnsi());
	if(!name.getLength())
	{
		sprintf(sName,"@%s:%s",structure->getNameFile().getAnsi(),structure->getName().getAnsi());
		
	}

	gUIManager->createNewTransferBox(structure,sName,caption,text,"Total Energy","To Deposit",playerPower,0,player,SUI_Window_Deposit_Power);
	
}


//============================================================================================
//	 transfers maintenance between player and structure
//

void StructureManager::createPayMaintenanceTransferBox(PlayerObject* player, PlayerStructure* structure)
{
	int32 structureFunds = structure->getCurrentMaintenance();

	int8 text[255];
	sprintf(text,"Select the total amount you would like to pay to the existing maintenance pool.\xa\xa Current maintenance pool: %u cr.",structureFunds);
	
	int8 caption[32];
	sprintf(caption,"SELECT AMOUNT");
	int8 sName[128];

	string name = structure->getCustomName();			
	name.convert(BSTRType_ANSI);
	sprintf(sName,"%s",name.getAnsi());
	if(!name.getLength())
	{
		sprintf(sName,"@%s:%s",structure->getNameFile().getAnsi(),structure->getName().getAnsi());
		
	}


	uint32 funds = dynamic_cast<Inventory*>(player->getEquipManager()->getEquippedObject(CreatureEquipSlot_Inventory))->getCredits();
	funds += dynamic_cast<Bank*>(player->getEquipManager()->getEquippedObject(CreatureEquipSlot_Bank))->getCredits();

	gUIManager->createNewTransferBox(structure,sName,caption,text,"Total Funds","To Pay",funds,structureFunds,player,SUI_Window_Pay_Maintenance);
	
}

void StructureManager::createNewStructureDeleteConfirmBox(PlayerObject* player, PlayerStructure* structure)
{

	string text = "Your structure";
	if(structure->getRedeed())
	{
		text <<"\\#006400WILL\\#FFFFFF ";
	}
	else
		text <<"\\#FF0000WILL NOT \\#FFFFFF";

	text <<"be redeeded. If you wish to continue with destroying your structure, please enter the following code into the input box.\xa\xa";
	
	int8 code [32];
	structure->setCode();
	sprintf(code,"code: %s",structure->getCode().getAnsi());
	text << code;

	int8 caption[32];
	sprintf(caption,"CONFIRM STRUCTURE DESTRUCTION");

	BStringVector vector;

	gUIManager->createNewInputBox(structure,"",caption,text.getAnsi(),vector,player,SUI_IB_NODROPDOWN_OKCANCEL,SUI_Window_Structure_Delete_Confirm,6);
	
}

void StructureManager::createNewStructureDestroyBox(PlayerObject* player, PlayerStructure* structure, bool redeed)
{
	BStringVector attributesMenu;

	string text = "You have elected to destroy a structure. Pertinent structure data can be found in the list below. Please complete the following steps to confirm structure deletion.\xa\xa";
			text <<"If you wish to redeed your structure, all structure data must be GREEN. \xa To continue with structure deletion, click YES. Otherwise, please click NO.\xa";
			
	if(structure->canRedeed())
	{
		text <<"WILL REDEED: \\#006400 YES \\#FFFFFF";			

		int8 redeedText[64];
		sprintf(redeedText,"CAN REDEED: \\#006400 YES\\#FFFFFF");
		attributesMenu.push_back(redeedText);
	}
	else
	{
		text <<"WILL REDEED: \\#FF0000 NO \\#FFFFFF";			

		int8 redeedText[64];
		sprintf(redeedText,"CAN REDEED: \\#FF0000 NO\\#FFFFFF");
		attributesMenu.push_back(redeedText);
	}
	
	uint32 maxCond = structure->getMaxCondition();
	uint32 cond = structure->getCondition();
		
	if( cond < maxCond)
	{
		int8 condition[64];
		sprintf(condition,"-CONDITION:\\#FF0000%u/%u\\#FFFFFF",cond,maxCond);
		attributesMenu.push_back(condition);
		
	}
	else
	{
		int8 condition[64];
		sprintf(condition,"-CONDITION:\\#006400%u/%u\\#FFFFFF",cond,maxCond);
		attributesMenu.push_back(condition);
	}


	uint32 maintIs = structure->getCurrentMaintenance();
	uint32 maintNeed = structure->getMaintenanceRate()*45;

	if(maintIs >= maintNeed)
	{
		int8 maintenance[128];
		sprintf(maintenance,"-MAINTENANCE:\\#006400%u/%u\\#FFFFFF",maintIs,maintNeed);
		attributesMenu.push_back(maintenance);
	}
	else
	{
		int8 maintenance[128];
		sprintf(maintenance,"-MAINTENANCE:\\#FF0000%u/%u\\#FFFFFF",maintIs,maintNeed);
		attributesMenu.push_back(maintenance);
	}

	int8 sName[128];

	string name = structure->getCustomName();			
	name.convert(BSTRType_ANSI);
	sprintf(sName,"%s",name.getAnsi());
	if(!name.getLength())
	{
		sprintf(sName,"@%s:%s",structure->getNameFile().getAnsi(),structure->getName().getAnsi());
		
	}

	//answer = x/(total/100);
	//answer = x*(total/100);
	// total = 100%
	
	gUIManager->createNewListBox(structure,"handle Structure Destroy",sName, text.getAnsi(), attributesMenu, player, SUI_Window_Structure_Delete,SUI_LB_OKCANCEL);
}


/*
 *  SFCFile.cpp
 *  openc2e
 *
 *  Created by Alyssa Milburn on Sat 21 Oct 2006.
 *  Copyright (c) 2006-2008 Alyssa Milburn. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 */

#include "SFCFile.h"

#include "Agent.h"
#include "Blackboard.h"
#include "CallButton.h"
#include "Camera.h"
#include "CompoundAgent.h"
#include "DullPart.h"
#include "Engine.h"
#include "Lift.h"
#include "Map.h"
#include "MetaRoom.h"
#include "PointerAgent.h"
#include "Room.h"
#include "SimpleAgent.h"
#include "Vehicle.h"
#include "World.h"
#include "caosScript.h"
#include "common/Exception.h"
#include "common/endianlove.h"
#include "common/macro_stringify.h"
#include "fileformats/sprImage.h"
#include "imageManager.h"

#include <cassert>
#include <fmt/core.h>
#include <iostream>
#include <limits.h>
#include <memory>

/*
 * sfcdumper.py has better commentary on this format - use it for debugging
 * and make sure to update it if you update this!
 */

#define TYPE_MAPDATA 1
#define TYPE_CGALLERY 2
#define TYPE_CDOOR 3
#define TYPE_CROOM 4
#define TYPE_ENTITY 5
#define TYPE_COMPOUNDOBJECT 6
#define TYPE_BLACKBOARD 7
#define TYPE_VEHICLE 8
#define TYPE_LIFT 9
#define TYPE_SIMPLEOBJECT 10
#define TYPE_POINTERTOOL 11
#define TYPE_CALLBUTTON 12
#define TYPE_SCENERY 13
#define TYPE_MACRO 14
#define TYPE_OBJECT 100

#define sfccheck(x) \
	if (!(x)) \
		throw Exception("failure while reading SFC file: '" #x "' at " __FILE__ ":" stringify(__LINE__));

SFCFile::~SFCFile() {
	// This contains all the objects we've constructed, so we can just zap this and
	// everything neatly disappears.
	for (auto& i : storage) {
		delete i;
	}
}

void SFCFile::read(std::istream* i) {
	ourStream = i;

	mapdata = (MapData*)slurpMFC(TYPE_MAPDATA);
	sfccheck(mapdata);

	// TODO: hackery to seek to the next bit
	uint8_t x = 0;
	while (x == 0)
		x = read8();
	ourStream->seekg(-1, std::ios::cur);

	uint32_t numobjects = read32();
	for (unsigned int i = 0; i < numobjects; i++) {
		SFCObject* o = (SFCObject*)slurpMFC(TYPE_OBJECT);
		sfccheck(o);
		objects.push_back(o);
	}

	uint32_t numscenery = read32();
	for (unsigned int i = 0; i < numscenery; i++) {
		SFCScenery* o = (SFCScenery*)slurpMFC(TYPE_SCENERY);
		sfccheck(o);
		scenery.push_back(o);
	}

	uint32_t numscripts = read32();
	for (unsigned int i = 0; i < numscripts; i++) {
		SFCScript x;
		x.read(this);
		scripts.push_back(x);
	}

	scrollx = read32();
	scrolly = read32();

	sfccheck(read16() == 0); // TODO
	favplacename = readstring();
	favplacex = read16();
	favplacey = read16();

	if (version() == 0)
		readBytes(25); // TODO
	else
		readBytes(29); // TODO

	uint16_t numspeech = read16();
	for (unsigned int i = 0; i < numspeech; i++) {
		speech_history.push_back(readstring());
	}

	uint32_t nomacros = read32();
	for (unsigned int i = 0; i < nomacros; i++) {
		SFCMacro* o = (SFCMacro*)slurpMFC(TYPE_MACRO);
		if (o) // TODO: ugh
			macros.push_back(o);
	}
}

bool validSFCType(unsigned int type, unsigned int reqtype) {
	if (reqtype == 0)
		return true;
	if (type == reqtype)
		return true;
	if ((reqtype == TYPE_OBJECT) && (type >= TYPE_COMPOUNDOBJECT))
		return true;
	if ((reqtype == TYPE_COMPOUNDOBJECT) && (type >= TYPE_COMPOUNDOBJECT) && (type <= TYPE_LIFT))
		return true;

	return false;
}

SFCClass* SFCFile::slurpMFC(unsigned int reqtype) {
	sfccheck(!ourStream->fail());

	// read the pid (this only works up to 0x7ffe, but we'll cope)
	uint16_t pid = read16();

	if (pid == 0) {
		// null object
		return 0;
	} else if (pid == 0xffff) {
		// completely new class, read details
		(void)read16(); // schemaid
		uint16_t strlen = read16();
		char* temp = new char[strlen];
		ourStream->read(temp, strlen);
		std::string classname(temp, strlen);
		delete[] temp;

		pid = storage.size();

		// push a null onto the stack
		storage.push_back(0);

		// set the types array as necessary
		if (classname == "MapData")
			types[pid] = TYPE_MAPDATA;
		else if (classname == "CGallery")
			types[pid] = TYPE_CGALLERY;
		else if (classname == "CDoor")
			types[pid] = TYPE_CDOOR;
		else if (classname == "CRoom")
			types[pid] = TYPE_CROOM;
		else if (classname == "Entity")
			types[pid] = TYPE_ENTITY;
		else if (classname == "CompoundObject")
			types[pid] = TYPE_COMPOUNDOBJECT;
		else if (classname == "Blackboard")
			types[pid] = TYPE_BLACKBOARD;
		else if (classname == "Vehicle")
			types[pid] = TYPE_VEHICLE;
		else if (classname == "Lift")
			types[pid] = TYPE_LIFT;
		else if (classname == "SimpleObject")
			types[pid] = TYPE_SIMPLEOBJECT;
		else if (classname == "PointerTool")
			types[pid] = TYPE_POINTERTOOL;
		else if (classname == "CallButton")
			types[pid] = TYPE_CALLBUTTON;
		else if (classname == "Scenery")
			types[pid] = TYPE_SCENERY;
		else if (classname == "Macro")
			types[pid] = TYPE_MACRO;
		else
			throw Exception(std::string("SFCFile doesn't understand class name '") + classname + "'!");
	} else if ((pid & 0x8000) != 0x8000) {
		// return an existing object
		pid -= 1;
		sfccheck(pid < storage.size());
		sfccheck(validSFCType(types[pid], reqtype));
		SFCClass* temp = storage[pid];
		sfccheck(temp);
		return temp;
	} else {
		// create a new object of an existing class
		pid ^= 0x8000;
		pid -= 1;
		sfccheck(pid < storage.size());
		sfccheck(!(storage[pid]));
	}

	SFCClass* newobj;

	// construct new object of specified type
	sfccheck(validSFCType(types[pid], reqtype));
	switch (types[pid]) {
		case TYPE_MAPDATA: newobj = new MapData(this); break;
		case TYPE_CGALLERY: newobj = new CGallery(this); break;
		case TYPE_CDOOR: newobj = new CDoor(this); break;
		case TYPE_CROOM: newobj = new CRoom(this); break;
		case TYPE_ENTITY: newobj = new SFCEntity(this); break;
		case TYPE_COMPOUNDOBJECT: newobj = new SFCCompoundObject(this); break;
		case TYPE_BLACKBOARD: newobj = new SFCBlackboard(this); break;
		case TYPE_VEHICLE: newobj = new SFCVehicle(this); break;
		case TYPE_LIFT: newobj = new SFCLift(this); break;
		case TYPE_SIMPLEOBJECT: newobj = new SFCSimpleObject(this); break;
		case TYPE_POINTERTOOL: newobj = new SFCPointerTool(this); break;
		case TYPE_CALLBUTTON: newobj = new SFCCallButton(this); break;
		case TYPE_SCENERY: newobj = new SFCScenery(this); break;
		case TYPE_MACRO: newobj = new SFCMacro(this); break;
		default:
			throw Exception("SFCFile didn't find a valid type in internal variable, argh!");
	}

	// push the object onto storage, and make it deserialize itself
	types[storage.size()] = types[pid];
	storage.push_back(newobj);
	newobj->read();

	// return this new object
	return newobj;
}

uint8_t SFCFile::read8() {
	return ::read8(*ourStream);
}

uint16_t SFCFile::read16() {
	return ::read16le(*ourStream);
}

uint32_t SFCFile::read32() {
	return ::read32le(*ourStream);
}

std::string SFCFile::readstring() {
	uint32_t strlen = read8();
	if (strlen == 0xff) {
		strlen = read16();
		if (strlen == 0xffff)
			strlen = read32();
	}

	return readBytes(strlen);
}

std::string SFCFile::readBytes(unsigned int n) {
	char* temp = new char[n];
	ourStream->read(temp, n);
	std::string t = std::string(temp, n);
	delete[] temp;
	return t;
}

void SFCFile::setVersion(unsigned int v) {
	if (v == 0) {
		sfccheck(engine.gametype == "c1");
	} else if (v == 1) {
		sfccheck(engine.gametype == "c2");
	} else {
		throw Exception(fmt::format("unknown version# {}", v));
	}

	ver = v;
}

// ------------------------------------------------------------------

void MapData::read() {
	// read version (0 = c1, 1 = c2)
	unsigned int ver = read32();
	sfccheck((ver == 0) || (ver == 1));
	parent->setVersion(ver);

	// discard unknown bytes
	read32();
	if (parent->version() == 1) {
		read32();
		read32();
	}

	// background sprite
	background = (CGallery*)slurpMFC(TYPE_CGALLERY);
	sfccheck(background);

	// room data
	uint32_t norooms = read32();
	for (unsigned int i = 0; i < norooms; i++) {
		if (parent->version() == 0) {
			CRoom* temp = new CRoom(parent);
			temp->id = i;
			temp->left = reads32();
			temp->top = read32();
			temp->right = reads32();
			temp->bottom = read32();
			temp->roomtype = read32();
			sfccheck(temp->roomtype < 3);
			rooms.push_back(temp);
		} else {
			CRoom* temp = (CRoom*)slurpMFC(TYPE_CROOM);
			if (temp)
				rooms.push_back(temp);
			else
				norooms++; // room got deleted
		}
	}

	// read groundlevel data
	if (parent->version() == 0) {
		for (unsigned int& groundlevel : groundlevels) {
			groundlevel = read32();
		}

		readBytes(800); // TODO
	}
}

MapData::~MapData() {
	// In C1, we create rooms which aren't MFC objects, so we need to destroy them afterwards.
	if (parent->version() == 0) {
		for (auto& room : rooms) {
			delete room;
		}
	}
}

void CGallery::read() {
	noframes = read32();
	filename = parent->readBytes(4);
	firstimg = read32();

	// discard unknown bytes
	read32();

	for (unsigned int i = 0; i < noframes; i++) {
		// discard unknown bytes
		read8();
		read8();
		read8();
		// discard width, height and offset
		read32();
		read32();
		read32();
	}
}

void CDoor::read() {
	openness = read8();
	otherroom = read16();

	// discard unknown bytes
	sfccheck(read16() == 0);
}

void CRoom::read() {
	id = read32();

	// magic constant?
	sfccheck(read16() == 2);

	left = read32();
	top = read32();
	right = read32();
	bottom = read32();

	for (auto& door : doors) {
		uint16_t nodoors = read16();
		for (unsigned int j = 0; j < nodoors; j++) {
			CDoor* temp = (CDoor*)slurpMFC(TYPE_CDOOR);
			sfccheck(temp);
			door.push_back(temp);
		}
	}

	roomtype = read32();
	sfccheck(roomtype < 4);

	floorvalue = read8();
	inorganicnutrients = read8();
	organicnutrients = read8();
	temperature = read8();
	heatsource = reads32();

	pressure = read8();
	pressuresource = reads32();

	windx = reads32();
	windy = reads32();

	lightlevel = read8();
	lightsource = reads32();

	radiation = read8();
	radiationsource = reads32();

	// discard unknown bytes
	readBytes(800);

	uint16_t nopoints = read16();
	for (unsigned int i = 0; i < nopoints; i++) {
		uint32_t x = read32();
		uint32_t y = read32();
		floorpoints.push_back(std::pair<uint32_t, uint32_t>(x, y));
	}

	// discard unknown bytes
	sfccheck(read32() == 0);

	music = readstring();
	dropstatus = read32();
	sfccheck(dropstatus < 3);
}

void SFCMacro::read() {
	readBytes(12); // TODO

	script = readstring();

	read32();
	read32();
	if (parent->version() == 0)
		readBytes(120);
	else
		readBytes(480);

	owner = (SFCObject*)slurpMFC(TYPE_OBJECT);
	sfccheck(owner);
	from = (SFCObject*)slurpMFC(TYPE_OBJECT);
	sfccheck(read16() == 0);
	targ = (SFCObject*)slurpMFC(TYPE_OBJECT);

	readBytes(18);

	if (parent->version() == 1)
		readBytes(16);
}

void SFCEntity::read() {
	// read sprite
	sprite = (CGallery*)slurpMFC(TYPE_CGALLERY);
	sfccheck(sprite);

	// read current frame and offset from base
	currframe = read8();
	imgoffset = read8();

	// read zorder, x, y
	zorder = reads32();
	x = read32();
	y = read32();

	// check if this agent is animated at present
	uint8_t animbyte = read8();
	if (animbyte) {
		sfccheck(animbyte == 1);
		haveanim = true;

		// read the animation frame
		animframe = read8();

		// read the animation string
		std::string tempstring;
		if (parent->version() == 0)
			tempstring = readBytes(32);
		else
			tempstring = readBytes(99);
		// chop off non-null-terminated bits
		animstring = std::string(tempstring.c_str());
	} else {
		haveanim = false;
	}
}

void SFCObject::read() {
	// read genus, family and species
	if (parent->version() == 0) {
		// sfccheck(read8() == 0);
		read8(); // discard unused portion of CLAS, i guess (so far has been 0 or 255)
		species = read8();
		genus = read8();
		family = read8();
	} else {
		genus = read8();
		family = read8();
		sfccheck(read16() == 0);
		species = read16();
	}

	if (parent->version() == 0) {
		// discard unknown byte
		read8();
	} else {
		// read UNID
		unid = read32();

		// discard unknown byte
		read8();
	}

	// read ATTR
	if (parent->version() == 0)
		attr = read8();
	else
		attr = read16();

	// discard unknown bytes
	if (parent->version() == 1)
		sfccheck(read16() == 0);

	// read unknown coords
	left = read32();
	top = read32();
	right = read32();
	bottom = read32();

	// discard unknown bytes
	read16();

	// read ACTV
	actv = read8();

	// read sprite
	sprite = (CGallery*)slurpMFC(TYPE_CGALLERY);
	sfccheck(sprite);

	tickreset = read32();
	tickstate = read32();
	sfccheck(tickreset >= tickstate);

	// discard unknown bytes
	sfccheck(read16() == 0);

	// read currently-looping sound, if any
	currentsound = readBytes(4);
	if (currentsound[0] == 0)
		currentsound.clear();

	// read object variables
	for (unsigned int i = 0; i < (parent->version() == 0 ? 3 : 100); i++)
		variables[i] = read32();

	if (parent->version() == 1) {
		// read physics values
		size = read8();
		range = read32();

		// TODO: this is mysterious and unknown
		gravdata = read32();

		// read physics values
		accg = read32();
		velx = reads32();
		vely = reads32();
		rest = read32();
		aero = read32();

		// discard unknown bytes
		readBytes(6);

		// read threats
		threat = read8();

		// read flags
		uint8_t flags = read8();
		frozen = (flags & 0x02);
	}

	// read scripts
	uint32_t numscripts = read32();
	for (unsigned int i = 0; i < numscripts; i++) {
		SFCScript x;
		x.read(parent);
		scripts.push_back(x);
	}
}

void SFCCompoundObject::read() {
	SFCObject::read();

	uint32_t numparts = read32();

	for (unsigned int i = 0; i < numparts; i++) {
		SFCEntity* part = (SFCEntity*)slurpMFC(TYPE_ENTITY);
		uint32_t relx = read32();
		uint32_t rely = read32();
		if (i == 0) {
			sfccheck(part);
			sfccheck(relx == 0 && rely == 0);
		}

		// push the entity, even if it is null..
		parts.push_back(part);
		parts_relx.push_back(relx);
		parts_rely.push_back(rely);
	}

	// read hotspot coordinates
	for (auto& hotspot : hotspots) {
		hotspot.left = reads32();
		hotspot.top = reads32();
		hotspot.right = reads32();
		hotspot.bottom = reads32();
	}

	// read hotspot function data
	for (auto& hotspot : hotspots) {
		// (this is actually a map of function->hotspot)
		hotspot.function = reads32();
	}

	if (parent->version() == 1) {
		// read C2-specific hotspot function data (again, actually maps function->hotspot)
		for (auto& hotspot : hotspots) {
			hotspot.message = read16();
			sfccheck(read16() == 0);
		}
		for (auto& hotspot : hotspots) {
			hotspot.mask = read8();
		}
	}
}

void SFCBlackboard::read() {
	SFCCompoundObject::read();

	// read text x/y position and colours
	if (parent->version() == 0) {
		backgroundcolour = read8();
		chalkcolour = read8();
		aliascolour = read8();
		textx = read8();
		texty = read8();
	} else {
		backgroundcolour = read32();
		chalkcolour = read32();
		aliascolour = read32();
		textx = read8();
		texty = read8();
	}

	// read blackboard strings
	for (unsigned int i = 0; i < (parent->version() == 0 ? 16 : 48); i++) {
		uint32_t value = read32();
		std::string str = readBytes(11);
		// chop off non-null-terminated bits
		str = std::string(str.c_str());
		strings.push_back(std::pair<uint32_t, std::string>(value, str));
	}
}

void SFCVehicle::read() {
	SFCCompoundObject::read();

	xvec = reads32();
	yvec = reads32();
	uint32_t xcoord = read32() / 256;
	uint32_t ycoord = read32() / 256;
	// TODO
	(void)xcoord;
	(void)ycoord;

	// read cabin boundaries
	cabinleft = read32();
	cabintop = read32();
	cabinright = read32();
	cabinbottom = read32();

	bump = read32();
}

void SFCLift::read() {
	SFCVehicle::read();

	nobuttons = read32();
	currentbutton = read32();

	// discard unknown bytes
	sfccheck(readBytes(5) == std::string("\xff\xff\xff\xff\x00", 5));

	for (unsigned int& i : callbuttony) {
		i = read32();

		// discard unknown bytes
		sfccheck(read16() == 0);
	}

	if (parent->version() == 1) {
		int somedata = read32();
		alignwithcabin = (somedata != 0); // TODO: is this right?
	}
}

void SFCSimpleObject::read() {
	SFCObject::read();

	entity = (SFCEntity*)slurpMFC(TYPE_ENTITY);

	// read part zorder
	partzorder = read32();

	// read bhvrclick
	bhvrclick[0] = (signed char)read8();
	bhvrclick[1] = (signed char)read8();
	bhvrclick[2] = (signed char)read8();

	// read BHVR touch
	bhvrtouch = read8();

	if (parent->version() == 0)
		return;

	// read pickup handles/points
	uint16_t num_pickup_handles = read16();
	for (unsigned int i = 0; i < num_pickup_handles; i++) {
		int x = reads32();
		int y = reads32();
		pickup_handles.push_back(std::pair<int, int>(x, y));
	}
	uint16_t num_pickup_points = read16();
	for (unsigned int i = 0; i < num_pickup_points; i++) {
		int x = reads32();
		int y = reads32();
		pickup_points.push_back(std::pair<int, int>(x, y));
	}
}

void SFCPointerTool::read() {
	SFCSimpleObject::read();

	// discard unknown bytes
	if (parent->version() == 0)
		readBytes(35);
	else
		readBytes(51);
}

void SFCCallButton::read() {
	SFCSimpleObject::read();

	ourLift = (SFCLift*)slurpMFC(TYPE_LIFT);
	liftid = read8();
}

void SFCScript::read(SFCFile* f) {
	if (f->version() == 0) {
		eventno = f->read8();
		species = f->read8();
		genus = f->read8();
		family = f->read8();
	} else {
		genus = f->read8();
		family = f->read8();
		eventno = f->read16();
		species = f->read16();
	}
	data = f->readstring();
}

// ------------------------------------------------------------------

void SFCFile::copyToWorld() {
	// add the map data
	mapdata->copyToWorld();

	// install scripts
	for (auto& script : scripts) {
		script.install();
	}

	// create normal objects
	for (auto& object : objects) {
		object->copyToWorld();
	}

	// create scenery
	for (auto& i : scenery) {
		i->copyToWorld();
	}

	// activate old scripts
	for (auto& macro : macros) {
		macro->activate();
	}

	// move the camera to the correct position
	engine.camera->moveTo(scrollx, scrolly, jump);

	// patch agents
	// TODO: do we really need to do this, and if so, should it be done here?
	// I like this for now because it makes debugging suck a lot less - fuzzie
	for (auto& agent : world.agents) {
		std::shared_ptr<Agent> a = agent;

#define NUM_SFC_PATCHES 5

		/* version, family, genus, species, variable# */
		unsigned int patchdata[NUM_SFC_PATCHES][5] = {
			{1, 2, 20, 10, 10}, // c2's Pitz
			{1, 2, 17, 2, 1}, // c2's bees
			{1, 2, 1, 50, 0}, // c2 flask thing
			{1, 2, 25, 1, 1}, // c2 tomatoes
			{1, 2, 25, 6, 1} // c2 nuts
		};

		for (auto& j : patchdata) {
			if (version() == j[0] && a->family == j[1] && a->genus == j[2] && a->species == j[3]) {
				// patch variable to actually refer to an agent
				unsigned int varno = j[4];
				for (auto& object : objects) {
					if (object->unid == (uint32_t)a->var[varno].getInt()) {
						a->var[varno].setAgent(object->copiedAgent());
						break;
					}
				}

				if (a->var[varno].hasInt()) {
					a->var[varno].setAgent(0);
					// This is useful to enable when you're testing a new patch.
					// std::cout << "Warning: Couldn't apply agent patch #" << j << "!" << std::endl;
				}
			}
		}
	}
}

void MapData::copyToWorld() {
	// create the global metaroom
	// TODO: hardcoded size bad?
	unsigned int w = parent->version() == 0 ? 1200 : 2400;
	MetaRoom* m = world.map->addMetaRoom(0, 0, 8352, w, background->filename, true);

	for (auto src : rooms) {
		// retrieve our room data
		// create a new room, set the type
		std::shared_ptr<Room> r(new Room(src->left, src->right, src->top, src->top, src->bottom, src->bottom));
		r->type = src->roomtype;

		// add the room to the world, ensure it matches the id we retrieved
		while (src->id > world.map->room_base)
			world.map->room_base++; // skip any gaps (deleted rooms)
		unsigned int roomid = m->addRoom(r);
		sfccheck(roomid == src->id);

		if (parent->version() == 1) {
			// set floor points
			r->floorpoints = src->floorpoints;

			// set CAs
			r->intr = src->inorganicnutrients;
			r->ontr = src->organicnutrients;
			r->temp = src->temperature;
			r->pres = src->pressure;
			r->lite = src->lightlevel;
			r->radn = src->radiation;
			r->hsrc = src->heatsource;
			r->psrc = src->pressuresource;
			r->lsrc = src->lightsource;
			r->rsrc = src->radiationsource;

			// set wind x/y
			r->windx = src->windx;
			r->windy = src->windy;

			r->dropstatus = src->dropstatus;
			r->floorvalue = src->floorvalue;

			r->music = src->music;
		}
	}

	if (parent->version() == 0) {
		for (unsigned int& groundlevel : groundlevels) {
			world.groundlevels.push_back(groundlevel);
		}

		return;
	}

	for (auto src : rooms) {
		for (auto& j : src->doors) {
			for (std::vector<CDoor*>::iterator k = j.begin(); k < j.end(); k++) {
				CDoor* door = *k;
				std::shared_ptr<Room> r1 = world.map->getRoom(src->id);
				std::shared_ptr<Room> r2 = world.map->getRoom(door->otherroom);

				if (!world.map->hasDoor(r1, r2)) {
					// create a new door between rooms!
					world.map->setDoorPerm(r1, r2, door->openness);
					// TODO: ADDR adds to nearby?
				} else {
					// sanity check
					sfccheck(world.map->getDoorPerm(r1, r2) == door->openness);
				}
			}
		}
	}

	// TODO: misc data?
}

void copyEntityData(SFCEntity* entity, DullPart* p) {
	// pose
	p->setBase(entity->imgoffset);
	p->setPose(entity->currframe - entity->imgoffset);

	// animation
	if (entity->haveanim) {
		for (char i : entity->animstring) {
			if (i == 'R')
				p->animation.push_back(255);
			else {
				sfccheck(i >= 48 && i <= 57);
				p->animation.push_back(i - 48);
			}
		}

		// TODO: should make sure animation position == current pose
		if (entity->animframe < p->animation.size()) {
			if (p->animation[entity->animframe] == 255)
				p->setFrameNo(0);
			else
				p->setFrameNo(entity->animframe);
		} else
			p->animation.clear();
	}

	if (entity->getParent()->version() == 0)
		return;
}

void SFCCompoundObject::copyToWorld() {
	sfccheck(parts.size() > 0);

	// construct our equivalent object, if necessary
	if (!ourAgent)
		ourAgent = new CompoundAgent(parts[0]->sprite->filename, parts[0]->sprite->firstimg, parts[0]->sprite->noframes);
	ourAgent->setClassifier(family, genus, species);

	// initialise the agent, move it into position
	CompoundAgent* a = ourAgent;
	a->finishInit();
	a->moveTo(parts[0]->x, parts[0]->y);
	a->queueScript(7); // enter scope

	if (parent->version() == 1)
		world.setUNID(a, unid);

	// TODO: c1 attributes!
	// C2 attributes are a subset of c2e ones
	a->setAttributes(attr);

	a->actv = actv;

	// ticking
	a->tickssincelasttimer = tickstate;
	if (a->tickssincelasttimer == tickreset)
		a->tickssincelasttimer = 0;
	a->timerrate = tickreset;

	for (unsigned int i = 0; i < (parent->version() == 0 ? 3 : 100); i++)
		a->var[i].setInt(variables[i]);

	if (parent->version() == 1) {
		a->size = size;
		a->thrt = threat;
		a->range = range;
		a->accg = accg;
		a->falling = (gravdata == 0xFFFFFFFF ? false : true); // TODO
		a->velx = velx;
		a->vely = vely;
		a->rest = rest;
		a->aero = aero;
		a->paused = frozen; // TODO
	}

	// make sure the zorder of the first part is 0..
	int basezorder = INT_MAX; // TODO: unsigned or signed?
	for (auto e : parts) {
		if (!e)
			continue;
		if (e->zorder < basezorder)
			basezorder = e->zorder;
	}

	for (unsigned int i = 0; i < parts.size(); i++) {
		SFCEntity* e = parts[i];
		if (!e)
			continue;
		DullPart* p;
		p = new DullPart(a, i, e->sprite->filename, e->sprite->firstimg, parts_relx[i], parts_rely[i], e->zorder - basezorder);
		a->addPart(p);

		copyEntityData(e, p);
	}

	// add hotspots
	for (unsigned int i = 0; i < 6; i++) {
		a->setHotspotLoc(i, hotspots[i].left, hotspots[i].top, hotspots[i].right, hotspots[i].bottom);
		a->setHotspotFunc(i, hotspots[i].function);
		if (parent->version() == 1) {
			a->setHotspotFuncDetails(i, hotspots[i].message, hotspots[i].mask);
		}
	}
}

void SFCSimpleObject::copyToWorld() {
	// construct our equivalent object
	if (!ourAgent) {
		ourAgent = new SimpleAgent(family, genus, species, entity->zorder, sprite->filename, sprite->firstimg, sprite->noframes);
	}
	SimpleAgent* a = ourAgent;

	a->finishInit();
	// a->moveTo(entity->x - (a->part(0)->getWidth() / 2), entity->y - (a->part(0) -> getHeight() / 2));
	a->moveTo(entity->x, entity->y);
	a->queueScript(7); // enter scope

	if (parent->version() == 1)
		world.setUNID(a, unid);

	// copy data from ourselves

	// TODO: c1 attributes!
	// C2 attributes are a subset of c2e ones
	a->setAttributes(attr);

	a->actv = actv;

	// copy bhvrclick data
	a->clac[0] = bhvrclick[0];
	a->clac[1] = bhvrclick[1];
	a->clac[2] = bhvrclick[2];

	// ticking
	a->tickssincelasttimer = tickstate;
	if (a->tickssincelasttimer == tickreset)
		a->tickssincelasttimer = 0;
	a->timerrate = tickreset;

	for (unsigned int i = 0; i < (parent->version() == 0 ? 3 : 100); i++)
		a->var[i].setInt(variables[i]);

	if (parent->version() == 1) {
		a->size = size;
		a->thrt = threat;
		a->range = range;
		a->accg = accg;
		a->falling = (gravdata == 0xFFFFFFFF ? false : true); // TODO
		a->velx = velx;
		a->vely = vely;
		a->rest = rest;
		a->aero = aero;
		a->paused = frozen; // TODO
	}

	// copy data from entity
	DullPart* p = (DullPart*)a->part(0);
	copyEntityData(entity, p);

	// TODO: bhvr

	// pickup handles/points
	for (unsigned int i = 0; i < pickup_handles.size(); i++) {
		int x = pickup_handles[i].first, y = pickup_handles[i].second;
		if (x != -1 || y != -1)
			a->carried_points[i] = pickup_handles[i];
	}

	for (unsigned int i = 0; i < pickup_points.size(); i++) {
		int x = pickup_points[i].first, y = pickup_points[i].second;
		if (x != -1 || y != -1)
			a->carry_points[i] = pickup_points[i];
	}

	if (currentsound.size() != 0) {
		a->playAudio(currentsound, true, true);
	}
}

void SFCPointerTool::copyToWorld() {
	world.hand()->setClassifier(family, genus, species);
	world.hand()->setZOrder(entity->zorder);
	world.hand()->setHotspot(2, 2); // TODO: we should really copy this out of the SFC file (the first two numbers in the unknown data?)

	ourAgent = world.hand();
	SFCSimpleObject::copyToWorld(); // TODO: think about this call some more, is it appropriate for an already-finished agent?
}

void SFCBlackboard::copyToWorld() {
	ourAgent = new Blackboard(parts[0]->sprite->filename, parts[0]->sprite->firstimg, parts[0]->sprite->noframes, textx, texty, backgroundcolour, chalkcolour, aliascolour);
	Blackboard* a = dynamic_cast<Blackboard*>(ourAgent);

	SFCCompoundObject::copyToWorld();

	for (unsigned int i = 0; i < strings.size(); i++) {
		a->addBlackboardString(i, strings[i].first, engine.translateWordlistWord(strings[i].second));
	}
}

void SFCVehicle::copyToWorld() {
	if (!ourAgent) {
		ourAgent = new Vehicle(parts[0]->sprite->filename, parts[0]->sprite->firstimg, parts[0]->sprite->noframes);
	}
	Vehicle* a = dynamic_cast<Vehicle*>(ourAgent);
	assert(a);

	SFCCompoundObject::copyToWorld();

	// set cabin rectangle and plane
	a->setCabinRect(cabinleft, cabintop, cabinright, cabinbottom);

	// set bump, xvec and yvec
	a->bump = bump;
	a->xvec = xvec;
	a->yvec = yvec;
}

void SFCLift::copyToWorld() {
	Lift* a = new Lift(parts[0]->sprite->filename, parts[0]->sprite->firstimg, parts[0]->sprite->noframes);
	ourAgent = a;

	SFCVehicle::copyToWorld();

	// set current button
	a->currentbutton = a->newbutton = currentbutton;

	// set call button y locations
	for (unsigned int i = 0; i < nobuttons; i++) {
		a->callbuttony.push_back(callbuttony[i]);
	}

	if (parent->version() == 1) {
		a->alignwithcabin = alignwithcabin;
	}
}

void SFCCallButton::copyToWorld() {
	CallButton* a = new CallButton(family, genus, species, entity->zorder, sprite->filename, sprite->firstimg, sprite->noframes);
	ourAgent = a;

	SFCSimpleObject::copyToWorld();

	// set lift
	sfccheck(ourLift->ourAgent);
	assert(dynamic_cast<Lift*>(ourLift->ourAgent));
	a->lift = ourLift->ourAgent;

	a->buttonid = liftid;
}

void SFCScenery::read() {
	SFCObject::read();
	entity = (SFCEntity*)slurpMFC(TYPE_ENTITY);
}

void SFCScenery::copyToWorld() {
	// construct our equivalent object
	if (!ourAgent) {
		ourAgent = new SimpleAgent(family, genus, species, entity->zorder, sprite->filename, sprite->firstimg, sprite->noframes);
	}
	SimpleAgent* a = ourAgent;

	a->finishInit();
	// a->moveTo(entity->x - (a->part(0)->getWidth() / 2), entity->y - (a->part(0) -> getHeight() / 2));
	a->moveTo(entity->x, entity->y);
	a->queueScript(7); // enter scope

	if (parent->version() == 1)
		world.setUNID(a, unid);

	// copy data from ourselves

	// TODO: is all of this really needed for scenery?

	// TODO: c1 attributes!
	// C2 attributes are a subset of c2e ones
	a->setAttributes(attr);

	a->actv = actv;

	// ticking
	a->tickssincelasttimer = tickstate;
	if (a->tickssincelasttimer == tickreset)
		a->tickssincelasttimer = 0;
	a->timerrate = tickreset;

	for (unsigned int i = 0; i < (parent->version() == 0 ? 3 : 100); i++)
		a->var[i].setInt(variables[i]);

	if (parent->version() == 1) {
		a->size = size;
		a->thrt = threat;
		a->range = range;
		a->accg = accg;
		a->falling = (gravdata == 0xFFFFFFFF ? false : true); // TODO
		a->velx = velx;
		a->vely = vely;
		a->rest = rest;
		a->aero = aero;
		a->paused = frozen; // TODO
	}

	// copy data from entity
	DullPart* p = (DullPart*)a->part(0);
	copyEntityData(entity, p);
	p->is_transparent = true;

	if (currentsound.size() != 0) {
		a->playAudio(currentsound, true, true);
	}
}

void SFCScript::install() {
	std::string scriptinfo = fmt::format("<SFC script {}, {}, {}: {}>", (int)family, (int)genus, species, eventno);
	caosScript script(engine.gametype, scriptinfo);
	try {
		script.parse(data);
		script.installInstallScript(family, genus, species, eventno);
		script.installScripts();
	} catch (Exception& e) {
		std::cerr << "installation of \"" << scriptinfo << "\" failed due to exception " << e.prettyPrint() << std::endl;
	}
}

void SFCMacro::activate() {
	assert(owner);
	Agent* ourAgent = owner->copiedAgent();
	assert(ourAgent);

	/*
	 * TODO:
	 *
	 * At the moment, this just starts scripts from the beginning, anew.
	 */

	for (auto& s : parent->scripts) {
		if (s.genus != ourAgent->genus)
			continue;
		if (s.family != ourAgent->family)
			continue;
		if (s.species != ourAgent->species)
			continue;

		if (s.data != script)
			continue; // TODO: no better way?

		// TODO: this is a horrible hack to get around the fact that fireScript enforces actv
		if (s.eventno != 0)
			ourAgent->actv = 0;
		else
			ourAgent->actv = 1;

		ourAgent->queueScript(s.eventno);
		return;
	}

	std::cout << "Warning: SFCFile failed to find the real script for an SFCMacro owned by " << ourAgent->identify() << std::endl;
}

/* vim: set noet: */

/*
 * MemCache.cpp
 *
Copyright (c) 2021 Collin Kidder, Michael Neuweiler, Charles Galpin

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include "MemCache.h"
#include <Watchdog_t4.h>

extern WDT_T4<WDT3> wdt;

MemCache::MemCache()
{
}

void MemCache::setup() {
    tickHandler.detach(this);
    for (int c = 0; c < NUM_CACHED_PAGES; c++) {
        pages[c].address = 0xFFFFFF; //maximum number. This is way over what our chip will actually support so it signals unused
        pages[c].age = 0;
        pages[c].dirty = false;
    }
    //WriteTimer = 0;

    tickHandler.attach(this, CFG_TICK_INTERVAL_MEM_CACHE);
}


//Handle aging of dirty pages and flushing of aged out dirty pages
void MemCache::handleTick()
{
    int c;
    cache_age();
    for (c = 0; c<NUM_CACHED_PAGES; c++) {
        if ((pages[c].age == MAX_AGE) && (pages[c].dirty)) {
            FlushPage(c);
            return;
        }
    }
}

//this function flushes the first dirty page it finds. It should try to wait until enough time as elapsed since
//a previous page has been written. Remember that page writes take about 7ms.
void MemCache::FlushSinglePage()
{
    int c;
    for (c = 0; c<NUM_CACHED_PAGES; c++) {
        if (pages[c].dirty) {
            cache_writepage(c);
            pages[c].dirty = false;
            pages[c].age = 0; //freshly flushed!
            return;
        }
    }
}

//Flush every dirty page. It will block for 10ms per page so maybe things will be blocked for a long, long time
//DO NOT USE THIS FUNCTION UNLESS YOU CAN ACCEPT THAT!
void MemCache::FlushAllPages()
{
    int c;
    for (c = 0; c < NUM_CACHED_PAGES; c++) {
        if (pages[c].dirty) { //found a dirty page so flush it
            cache_writepage(c);
            pages[c].dirty = false;
            delay(10); //10ms is longest it would take to write a page according to datasheet
            wdt.feed();
        }
    }
}

//Flush a given page by the page ID. This is NOT by address so act accordingly. Likely no external code should ever use this
void MemCache::FlushPage(uint8_t page) {
    if (pages[page].dirty) {
        cache_writepage(page);
        pages[page].dirty = false;
        pages[page].age = 0; //freshly flushed!
    }
}

//Flush a page by taking an address within the page.
void MemCache::FlushAddress(uint32_t address) {
    uint32_t addr;
    uint8_t c;

    addr = address >> 8; //kick it down to the page we're talking about
    c = cache_hit(addr);
    if (c != 0xFF) FlushPage(c);
}

//Like FlushPage but also marks the page invalid (unused) so if another read request comes it it'll have to be re-read from EEPROM
void MemCache::InvalidatePage(uint8_t page)
{
    if (page > NUM_CACHED_PAGES - 1) return; //invalid page, buddy!
    if (pages[page].dirty) {
        cache_writepage(page);
    }
    pages[page].dirty = false;
    pages[page].address = 0xFFFFFF;
    pages[page].age = 0;
}

//Mark a given page unused given an address within that page. Will write the page out if it was dirty.
void MemCache::InvalidateAddress(uint32_t address)
{
    uint32_t addr;
    uint8_t c;

    addr = address >> 8; //kick it down to the page we're talking about
    c = cache_hit(addr);
    if (c != 0xFF) InvalidatePage(c);
}

//Mark all page cache entries unused (go back to clean slate). It will try to write any dirty pages so be prepared to wait.
void MemCache::InvalidateAll()
{
    uint8_t c;
    for (c=0; c<NUM_CACHED_PAGES; c++) {
        InvalidatePage(c);
        wdt.feed();
    }
}

//Cause a given page to be fully aged which will cause it to be written at the next opportunity
void MemCache::AgeFullyPage(uint8_t page)
{
    if (page < NUM_CACHED_PAGES) { //if we did indeed have that page in cache
        pages[page].age = MAX_AGE;
    }
}

//Cause the page containing the given address to be fully aged and thus written as soon as possible.
void MemCache::AgeFullyAddress(uint32_t address)
{
    uint8_t thisCache;
    uint32_t page_addr;

    page_addr = address >> 8; //kick it down to the page we're talking about
    thisCache = cache_hit(page_addr);

    if (thisCache != 0xFF) { //if we did indeed have that page in cache
        pages[thisCache].age = MAX_AGE;
    }
}

//Write data into the memory cache. Takes the place of direct EEPROM writes
//There are lots of versions of this
boolean MemCache::Write(uint32_t address, uint8_t valu)
{
    uint32_t addr;
    uint8_t c;

    addr = address >> 8; //kick it down to the page we're talking about
    c = cache_hit(addr);
    if (c == 0xFF) 	{
        c = cache_findpage(); //try to free up a page
        if (c != 0xFF) c = cache_readpage(addr); //and populate it with the existing data
    }
    if (c != 0xFF) {
        pages[c].data[(uint16_t)(address & 0x00FF)] = valu;
        pages[c].dirty = true;
        pages[c].address = addr; //set this in case we actually are setting up a new cache page
        return true;
    }
    return false;
}

boolean MemCache::Write(uint32_t address, uint16_t valu)
{
    boolean result;
    result = Write(address, &valu, 2);
    return result;
}

boolean MemCache::Write(uint32_t address, uint32_t valu)
{
    boolean result;
    result = Write(address, &valu, 4);
    return result;
}

boolean MemCache::Write(uint32_t address, float valu)
{
    boolean result;
    result = Write(address, &valu, 4);
    return result;
}

boolean MemCache::Write(uint32_t address, const void* data, uint16_t len)
{
    uint32_t addr;
    uint8_t c;
    uint16_t count;

    for (count = 0; count < len; count++) {
        addr = (address+count) >> 8; //kick it down to the page we're talking about
        c = cache_hit(addr);
        if (c == 0xFF) {
            c = cache_findpage(); //try to find a page that either isn't loaded or isn't dirty
            if (c != 0xFF) c = cache_readpage(addr); //and populate it with the existing data
        }
        if (c != 0xFF) { //could we find a suitable cache page to write to?
            pages[c].data[(uint16_t)((address+count) & 0x00FF)] = *(uint8_t *)( ((uint8_t *)data) + count);
            pages[c].dirty = true;
            pages[c].address = addr; //set this in case we actually are setting up a new cache page
        }
        else break;
    }

    if (c != 0xFF) return true; //all ok!
    return false;
}

boolean MemCache::Read(uint32_t address, uint8_t* valu)
{
    uint32_t addr;
    uint8_t c;

    addr = address >> 8; //kick it down to the page we're talking about
    c = cache_hit(addr);

    if (c == 0xFF) { //page isn't cached. Search the cache, potentially dump a page and bring this one in
        c = cache_readpage(addr);
    }

    if (c != 0xFF) {
        *valu = pages[c].data[(uint16_t)(address & 0x00FF)];
        if (!pages[c].dirty) pages[c].age = 0; //reset age since we just used it
        return true; //all ok!
    }
    else {
        return false;
    }
}

boolean MemCache::Read(uint32_t address, uint16_t* valu)
{
    boolean result;
    result = Read(address, valu, 2);
    return result;
}

boolean MemCache::Read(uint32_t address, uint32_t* valu)
{
    boolean result;
    result = Read(address, valu, 4);
    return result;
}

boolean MemCache::Read(uint32_t address, float* valu)
{
    boolean result;
    result = Read(address, valu, 4);
    return result;
}

boolean MemCache::Read(uint32_t address, void* data, uint16_t len)
{
    uint32_t addr;
    uint8_t c;
    uint16_t count;

    for (count = 0; count < len; count++) {
        addr = (address + count) >> 8;
        c = cache_hit(addr);
        if (c == 0xFF) { //page isn't cached. Search the cache, potentially dump a page and bring this one in
            c = cache_readpage(addr);
        }
        if (c != 0xFF) {
            *(uint8_t *)( ((uint8_t *)data) + count) = pages[c].data[(uint16_t)((address + count) & 0x00FF)];
            if (!pages[c].dirty) pages[c].age = 0; //reset age since we just used it
        }
        else break; //bust the for loop if we run into trouble
    }

    if (c != 0xFF) return true; //all ok!
    return false;
}

boolean MemCache::isWriting()
{
    //if (WriteTimer) return true;
    return false;
}

uint8_t MemCache::cache_hit(uint32_t address)
{
    uint8_t c;
    for (c = 0; c < NUM_CACHED_PAGES; c++) {
        if (pages[c].address == address) {
            return c;
        }
    }
    return 0xFF;
}

void MemCache::cache_age()
{
    uint8_t c;
    for (c = 0; c < NUM_CACHED_PAGES; c++) {
        if (pages[c].age < MAX_AGE) {
            pages[c].age++;
        }
    }
}

//try to find an empty page or one that can be removed from cache
uint8_t MemCache::cache_findpage()
{
    uint8_t c;
    uint8_t old_c, old_v;
    for (c = 0; c<NUM_CACHED_PAGES; c++) {
        if (pages[c].address == 0xFFFFFF) { //found an empty cache page so populate it and return its number
            pages[c].age = 0;
            pages[c].dirty = false;
            return c;
        }
    }
    //if we got here then there are no free pages so scan to find the oldest one which isn't dirty
    old_c = 0xFF;
    old_v = 0;
    for (c = 0; c<NUM_CACHED_PAGES; c++) {
        if (!pages[c].dirty && pages[c].age >= old_v) {
            old_c = c;
            old_v = pages[c].age;
        }
    }
    if (old_c == 0xFF) { //no pages were not dirty - try to free one up
        FlushSinglePage(); //try to free up a page
        //now try to find the free page (if one was freed)
        old_v = 0;
        for (c=0; c<NUM_CACHED_PAGES; c++) {
            if (!pages[c].dirty && pages[c].age >= old_v) {
                old_c = c;
                old_v = pages[c].age;
            }
        }
        if (old_c == 0xFF) return 0xFF; //if nothing worked then give up
    }

    //If we got to this point then we have a page to use
    pages[old_c].age = 0;
    pages[old_c].dirty = false;
    pages[old_c].address = 0xFFFFFF; //mark it unused

    return old_c;
}

uint8_t MemCache::cache_readpage(uint32_t addr)
{
    uint16_t c,d,e;
    uint32_t address = addr << 8;
    uint8_t buffer[3];
    uint8_t i2c_id;
    c = cache_findpage();
    Logger::avalanche("ReadPage");
    if (c != 0xFF) {
        buffer[0] = ((address & 0xFF00) >> 8);
        //buffer[1] = (address & 0x00FF);
        buffer[1] = 0; //the pages are 256 bytes so the start of a page is always 00 for the LSB
        i2c_id = 0b01010000 + ((address >> 16) & 0x03); //10100 is the chip ID then the two upper bits of the address
        Wire.beginTransmission(i2c_id);
        Wire.write(buffer, 2);
        Wire.endTransmission(false); //do NOT generate stop
        //delayMicroseconds(50); //give TWI some time to send and chip some time to get page
        Wire.requestFrom(i2c_id, 256); //this will generate stop though.
        for (e = 0; e < 256; e++)
        {
            if(Wire.available())
            {
                d = Wire.read(); // receive a byte as character
                pages[c].data[e] = d;
            }
        }
        pages[c].address = addr;
        pages[c].age = 0;
        pages[c].dirty = false;
    }
    return c;
}

boolean MemCache::cache_writepage(uint8_t page)
{
    uint16_t d;
    uint32_t addr;
    uint8_t buffer[258];
    uint8_t i2c_id;
    addr = pages[page].address << 8;
    buffer[0] = ((addr & 0xFF00) >> 8);
    //buffer[1] = (addr & 0x00FF);
    buffer[1] = 0; //pages are 256 bytes so LSB is always 0 for the start of a page
    i2c_id = 0b01010000 + ((addr >> 16) & 0x03); //10100 is the chip ID then the two upper bits of the address
    for (d = 0; d < 256; d++) {
        buffer[d + 2] = pages[page].data[d];
    }
    Wire.beginTransmission(i2c_id);
    Wire.write(buffer, 258);
    Wire.endTransmission(true);
    return true;
}

//Nuke it from orbit. It's the only way to be sure.
//erases the entire EEPROM back to 0xFF across all addresses. You will lose everything.
//There is no erase command on our EEPROM chip so you must manually write FF's to every addss

void MemCache::nukeFromOrbit()
{
    uint16_t d;
    uint32_t addr;
    uint8_t buffer[258];
    uint8_t i2c_id;

    for (d = 0; d < 256; d++) buffer[d+2] = 0xFF;

    for (int page = 0; page < 1024; page++)
    {
        addr = page * 256;
        buffer[0] = ((addr & 0xFF00) >> 8);
        buffer[1] = 0; //pages are 256 bytes so LSB is always 0 for the start of a page
        i2c_id = 0b01010000 + ((addr >> 16) & 0x03); //10100 is the chip ID then the two upper bits of the address
        Wire.beginTransmission(i2c_id);
        Wire.write(buffer, 258);
        Wire.endTransmission(true);
        delay(11);
        wdt.feed();
    }
}



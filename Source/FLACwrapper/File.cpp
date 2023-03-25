/*  Copyright (c) MediaArea.net SARL. All Rights Reserved.
 *
 *  Use of this source code is governed by a zlib-style license that can
 *  be found in the License.txt file in the root of the source tree.
 */

#include "ZenLib/File.h"
#include "ZenLib/FileName.h"
#include "FLACwrapper/File.h"
#include <dlfcn.h>
//---------------------------------------------------------------------------

namespace FLACwrapper
{


#define PREPARE_FUNCTION_POINTERS                   \
    FLAC_available = false;                         \
    FLAChandle = dlopen("libFLAC.so",RTLD_LAZY);    \
    if(!FLAChandle) {                               \
        fprintf(stderr,"Failed to open libFLAC\n"); \
    }                                               \
    else {                                          \
        FLACversion = (const char **) dlsym(FLAChandle, "FLAC__VERSION_STRING"); \
        local_FLAC_metadata_simple_iterator_new = (FLAC__Metadata_SimpleIterator * (*)()) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_new"); \
        local_FLAC_metadata_simple_iterator_delete = (void (*)(FLAC__Metadata_SimpleIterator *)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_delete"); \
        local_FLAC_metadata_simple_iterator_status = (FLAC__Metadata_SimpleIteratorStatus (*)(FLAC__Metadata_SimpleIterator *)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_status"); \
        local_FLAC_metadata_simple_iterator_init = (FLAC__bool (*)(FLAC__Metadata_SimpleIterator *, const char *, FLAC__bool, FLAC__bool)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_init"); \
        local_FLAC_metadata_simple_iterator_next = (FLAC__bool (*)(FLAC__Metadata_SimpleIterator *)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_next"); \
        local_FLAC_metadata_simple_iterator_prev = (FLAC__bool (*)(FLAC__Metadata_SimpleIterator *)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_prev"); \
        local_FLAC_metadata_simple_iterator_is_last = (FLAC__bool (*)(FLAC__Metadata_SimpleIterator *)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_is_last"); \
        local_FLAC_metadata_simple_iterator_get_block_type = (FLAC__MetadataType (*)(FLAC__Metadata_SimpleIterator *)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_get_block_type"); \
        local_FLAC_metadata_simple_iterator_get_application_id = (FLAC__bool (*)(FLAC__Metadata_SimpleIterator *, FLAC__byte *)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_get_application_id"); \
        local_FLAC_metadata_simple_iterator_get_block = (FLAC__StreamMetadata * (*)(FLAC__Metadata_SimpleIterator *)) dlsym(FLAChandle, "FLAC__metadata_simple_iterator_get_block"); \
        local_FLAC_metadata_object_delete = (void (*)(FLAC__StreamMetadata *)) dlsym(FLAChandle, "FLAC__metadata_object_delete"); \
        if(dlerror() == NULL)                       \
            FLAC_available = true;                  \
    }


// ***************************************************************************
// Constructor/Destructor
// ***************************************************************************

//---------------------------------------------------------------------------
File::File()
{
    File_Handle = ZenLib::File();
    PREPARE_FUNCTION_POINTERS
}

File::File(ZenLib::Ztring File_Name, ZenLib::File::access_t Access)
{
    File_Handle = ZenLib::File(File_Name, Access);
    PREPARE_FUNCTION_POINTERS
}


//---------------------------------------------------------------------------
File::~File()
{
    if(chunkbuffer != NULL)
        free(chunkbuffer);
    dlclose(FLAChandle);
}

// ***************************************************************************
// Open/Close
// ***************************************************************************

//---------------------------------------------------------------------------
bool File::Open (const ZenLib::tstring &File_Name_, ZenLib::File::access_t Access)
{
    ZenLib::FileName filename = File_Name_;
    if(filename.Extension_Get() == ZenLib::Ztring("flac")) {
        /* FLAC parsing */
        FLAC__StreamMetadata * block;
        void * temp_ptr;

        if(!FLAC_available)
            return false;

        if(Access != ZenLib::File::access_t::Access_Read)
            return false;

        FLAC__Metadata_SimpleIterator * iterator = local_FLAC_metadata_simple_iterator_new();
        if(iterator == NULL)
            return false;
        if(!local_FLAC_metadata_simple_iterator_init(iterator, ZenLib::Ztring(File_Name_).To_UTF8().c_str(), true /* read only */, true /* preserve file stats */))
            return false;
        do {
            FLAC__byte id[4];
            if(local_FLAC_metadata_simple_iterator_get_block_type(iterator) != FLAC__METADATA_TYPE_APPLICATION)
                continue;
            if(!local_FLAC_metadata_simple_iterator_get_application_id(iterator, id))
                return false;
            if(memcmp(id,"riff",4) != 0)
                continue;
            block = local_FLAC_metadata_simple_iterator_get_block(iterator);
            if(block == NULL)
                return false;
            if(block->length < 12) {
                local_FLAC_metadata_object_delete(block);
                continue;
            }
            temp_ptr = realloc(chunkbuffer,chunkbuffer_size + block->length - 4);
            if(temp_ptr == NULL)
                return false;
            chunkbuffer = (char *)temp_ptr;
            memcpy(chunkbuffer + chunkbuffer_size, block->data.application.data,block->length - 4);
            chunkbuffer_size += block->length - 4;
            if(chunkbuffer_data_location == 0)
                // Check whether this is the data chunk
                if(memcmp(block->data.application.data,"data",4) == 0) {
                    chunkbuffer_data_location = chunkbuffer_size;
                    chunkbuffer_data_length = (ZenLib::int64u)(block->data.application.data[4]) +
                                              ((ZenLib::int64u)(block->data.application.data[5]) << 8) + 
                                              ((ZenLib::int64u)(block->data.application.data[6]) << 16) +
                                              ((ZenLib::int64u)(block->data.application.data[7]) << 24);
                }
            local_FLAC_metadata_object_delete(block);
        } while(local_FLAC_metadata_simple_iterator_next(iterator));
        
        if(chunkbuffer == NULL || chunkbuffer_data_location == 0){
            // Haven't found data chunk, or any chunks at all
            return false;
        }
        return true;
    }
    else
        return File_Handle.Open(File_Name_, Access);
}
//---------------------------------------------------------------------------
bool File::Create (const ZenLib::Ztring &File_Name_, bool OverWrite)
{
    return File_Handle.Create(File_Name_, OverWrite);
}

//---------------------------------------------------------------------------
void File::Close ()
{
    File_Handle.Close();
}

// ***************************************************************************
// Read/Write
// ***************************************************************************

//---------------------------------------------------------------------------
size_t File::Read (ZenLib::int8u* Buffer, size_t Buffer_Size_Max)
{
    if(chunkbuffer == NULL)
        return File_Handle.Read(Buffer, Buffer_Size_Max);

    if(chunkbuffer_readpointer < chunkbuffer_data_location) {
        // Data to be read is all in front of audio
        size_t read_size = chunkbuffer_data_location - chunkbuffer_readpointer;
        if(read_size > Buffer_Size_Max)
            read_size = Buffer_Size_Max;
        
        memcpy(Buffer, chunkbuffer + chunkbuffer_readpointer, read_size);
        chunkbuffer_readpointer += read_size;
        return read_size;
    }
    else if(chunkbuffer_readpointer < chunkbuffer_data_location + chunkbuffer_data_length) {
        // Data to be read is audio
        // For now return empty data
        size_t read_size = chunkbuffer_data_location + chunkbuffer_data_length - chunkbuffer_readpointer;
        if(read_size > Buffer_Size_Max)
            read_size = Buffer_Size_Max;
        
        memset(Buffer, 0, read_size);
        chunkbuffer_readpointer += read_size;
        return read_size;
    }
    else {
        // Data to be read is behind audio
        size_t read_size = chunkbuffer_size + chunkbuffer_data_length - chunkbuffer_readpointer;
        if(read_size > Buffer_Size_Max)
            read_size = Buffer_Size_Max;

        memcpy(Buffer, chunkbuffer + chunkbuffer_readpointer - chunkbuffer_data_length, read_size);
        chunkbuffer_readpointer += read_size;
        return read_size;
    }
}

//---------------------------------------------------------------------------
size_t File::Write (const ZenLib::int8u* Buffer, size_t Buffer_Size)
{
    return File_Handle.Write(Buffer, Buffer_Size);
}

//---------------------------------------------------------------------------
bool File::Truncate (ZenLib::int64u Offset)
{
    return File_Handle.Truncate(Offset);
}

//---------------------------------------------------------------------------
size_t File::Write (const ZenLib::Ztring &ToWrite)
{
    return File_Handle.Write(ToWrite);
}

// ***************************************************************************
// Moving
// ***************************************************************************

//---------------------------------------------------------------------------
bool File::GoTo (ZenLib::int64s Position_ToMove, ZenLib::File::move_t MoveMethod)
{
    if(chunkbuffer == NULL)
        return File_Handle.GoTo(Position_ToMove, MoveMethod);

    if(MoveMethod == ZenLib::File::move_t::FromBegin) {
        chunkbuffer_readpointer = Position_ToMove;
        return true;
    }
    else if(MoveMethod == ZenLib::File::move_t::FromCurrent) {
        chunkbuffer_readpointer += Position_ToMove;
        return true;
    }
    else /*if(MoveMethod == ZenLib::File::move_t::FromEnd)*/ {
        chunkbuffer_readpointer = (ZenLib::File::move_t)chunkbuffer_data_length + Position_ToMove;
        return true;
    }
}

//---------------------------------------------------------------------------
ZenLib::int64u File::Position_Get ()
{
    if(chunkbuffer == NULL)
        return File_Handle.Position_Get();
    
    return chunkbuffer_readpointer;
}

// ***************************************************************************
// Attributes
// ***************************************************************************

//---------------------------------------------------------------------------
ZenLib::int64u File::Size_Get()
{
    if(chunkbuffer == NULL)
        return File_Handle.Size_Get();
    return chunkbuffer_size + chunkbuffer_data_length;
}

//---------------------------------------------------------------------------
ZenLib::Ztring File::Created_Get()
{
    return File_Handle.Created_Get();
}

//---------------------------------------------------------------------------
ZenLib::Ztring File::Created_Local_Get()
{
    return File_Handle.Created_Local_Get();
}

//---------------------------------------------------------------------------
ZenLib::Ztring File::Modified_Get()
{
    return File_Handle.Modified_Get();
}

//---------------------------------------------------------------------------
ZenLib::Ztring File::Modified_Local_Get()
{
    return File_Handle.Modified_Local_Get();
}

//---------------------------------------------------------------------------
bool File::Opened_Get()
{
    return File_Handle.Opened_Get();
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
ZenLib::int64u File::Size_Get(const ZenLib::Ztring &File_Name)
{
    ZenLib::File F(File_Name);
    return F.Size_Get();
}

//---------------------------------------------------------------------------
ZenLib::Ztring File::Created_Get(const ZenLib::Ztring &File_Name)
{
    ZenLib::File F(File_Name);
    return F.Created_Get(File_Name);
}

//---------------------------------------------------------------------------
ZenLib::Ztring File::Modified_Get(const ZenLib::Ztring &File_Name)
{
    ZenLib::File F(File_Name);
    return F.Modified_Get(File_Name);
}

//---------------------------------------------------------------------------
bool File::Exists(const ZenLib::Ztring &File_Name)
{
    ZenLib::File F(File_Name);
    return F.Exists(File_Name);
}

//---------------------------------------------------------------------------
bool File::Copy(const ZenLib::Ztring &Source, const ZenLib::Ztring &Destination, bool OverWrite)
{
    return ZenLib::File::Copy(Source, Destination, OverWrite);
}

//---------------------------------------------------------------------------
bool File::Move(const ZenLib::Ztring &Source, const ZenLib::Ztring &Destination, bool OverWrite)
{
    return ZenLib::File::Move(Source, Destination, OverWrite);
}

//---------------------------------------------------------------------------
bool File::Delete(const ZenLib::Ztring &File_Name)
{
    return ZenLib::File::Delete(File_Name);
}

//***************************************************************************
//
//***************************************************************************

} //namespace

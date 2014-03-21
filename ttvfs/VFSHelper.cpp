// VFSHelper.cpp - glues it all together and makes use simple
// For conditions of distribution and use, see copyright notice in VFS.h

#include <iostream> // for debug only, see EOF

#include "VFSInternal.h"
#include "VFSHelper.h"
#include "VFSTools.h"

#include "VFSDirInternal.h"
#include "VFSFile.h"
#include "VFSLoader.h"
#include "VFSArchiveLoader.h"

#ifdef _DEBUG
#  include <cassert>
#  define DEBUG_ASSERT(x) assert(x)
#else
#  define DEBUG_ASSERT(x)
#endif


VFS_NAMESPACE_START

// predecl is in VFS.h
bool _checkCompatInternal(_AbiCheck *used)
{
    if(sizeof(_AbiCheck) != used->structSize)
        return false;

    _AbiCheck here;
    memset(&here, 0, sizeof(here));
    here.structSize = sizeof(here);
    here.vfsposSize = sizeof(vfspos);

#ifdef VFS_LARGEFILE_SUPPORT
    here.largefile = 1;
#endif

#ifdef VFS_IGNORE_CASE
    here.nocase = 1;
#endif

    return !memcmp(&here, used, sizeof(here));
}

VFSHelper::VFSHelper()
: merged(new InternalDir("/"))
{
}

VFSHelper::~VFSHelper()
{
    Clear();
}

void VFSHelper::Clear(void)
{
    _cleanup();

    vlist.clear();
    loaders.clear();
    archLdrs.clear();
}


void VFSHelper::_cleanup(void)
{
    merged->_clearDirs();
}

bool VFSHelper::Mount(const char *src, const char *dest)
{
    return AddVFSDir(GetDir(src, false), dest);
}

bool VFSHelper::AddVFSDir(DirBase *dir, const char *subdir /* = NULL */)
{
    if(!dir)
        return false;
    if(!subdir)
        subdir = dir->fullname();

    VDirEntry ve(dir, subdir);
    _StoreMountPoint(ve);

    DirBase *sd = GetDir(subdir, true);
    if(!sd) // may be NULL if Prepare() was not called before
        return false;
    //sd->merge(dir, overwrite, Dir::MOUNTED); // merge into specified subdir. will be (virtually) created if not existing
    assert(false); // FIXME

    return true;
}

bool VFSHelper::Unmount(const char *src, const char *dest)
{
    DirBase *vd = GetDir(src, false);
    if(!vd)
        return false;

    VDirEntry ve(vd, dest);
    if(!_RemoveMountPoint(ve))
        return false;

    // FIXME: this could be done more efficiently by just reloading parts of the tree that were involved.
    //Reload(false, true, false);
    assert(false); // FIXME
    return true;
}

void VFSHelper::_StoreMountPoint(const VDirEntry& ve)
{
    // scan through and ensure only one mount point with the same data is present.
    // if present, remove and re-add, this ensures the mount point is at the end of the list
    for(VFSMountList::iterator it = vlist.begin(); it != vlist.end(); )
    {
        const VDirEntry& oe = *it;
        if (ve.mountPoint == oe.mountPoint
            && (ve.vdir == oe.vdir || !casecmp(ve.vdir->fullname(), oe.vdir->fullname())) )
        {
            vlist.erase(it++); // do not break; just in case there are more (fixme?)
        }
        else
            ++it;
    }

    vlist.push_back(ve);
}

bool VFSHelper::_RemoveMountPoint(const VDirEntry& ve)
{
    for(VFSMountList::iterator it = vlist.begin(); it != vlist.end(); ++it)
    {
        const VDirEntry& oe = *it;
        if(ve.mountPoint == oe.mountPoint
            && (ve.vdir == oe.vdir || !casecmp(ve.vdir->fullname(), oe.vdir->fullname())) )
        {
            vlist.erase(it);
            return true;
        }
    }
    return false;
}

bool VFSHelper::MountExternalPath(const char *path, const char *where /* = "" */)
{
    DiskDir *vfs = new DiskDir(path);
    return AddVFSDir(vfs, where);
}

void VFSHelper::AddLoader(VFSLoader *ldr)
{
    loaders.push_back(ldr);
}

void VFSHelper::AddArchiveLoader(VFSArchiveLoader *ldr)
{
    archLdrs.push_back(ldr);
}

Dir *VFSHelper::AddArchive(const char *arch, void *opaque /* = NULL */)
{
    File *af = GetFile(arch);
    if(!af)
        return NULL;

    Dir *ad = NULL;
    VFSLoader *fileLdr = NULL;
    for(ArchiveLoaderArray::iterator it = archLdrs.begin(); it != archLdrs.end(); ++it)
        if((ad = (*it)->Load(af, &fileLdr, opaque)))
            break;
    if(!ad)
        return NULL;

    if(fileLdr)
        loaders.push_back(fileLdr);

    AddVFSDir(ad, arch);

    return ad;
}

inline static File *VFSHelper_GetFileByLoader(VFSLoader *ldr, const char *fn, const char *unmangled, DirBase *root)
{
    if(!ldr)
        return NULL;
    File *vf = ldr->Load(fn, unmangled);
    if(vf)
        root->addRecursive(vf);
    return vf;
}

File *VFSHelper::GetFile(const char *fn)
{
    const char *unmangled = fn;
    std::string fixed = fn; // TODO: get rid of allocation here
    FixPath(fixed);
    fn = fixed.c_str();

    File *vf = NULL;

    vf = merged->getFile(fn);

    // nothing found? maybe a loader has something.
    // if so, add the newly created File to the tree.
    if(!vf)
        for(LoaderArray::iterator it = loaders.begin(); it != loaders.end(); ++it)
            if((vf = VFSHelper_GetFileByLoader(*it, fn, unmangled, GetDirRoot())))
                break;

    //printf("VFS: GetFile '%s' -> '%s' (%s:%p)\n", fn, vf ? vf->fullname() : "NULL", vf ? vf->getType() : "?", vf);

    return vf;
}

inline static Dir *VFSHelper_GetDirByLoader(VFSLoader *ldr, const char *fn, const char *unmangled, Dir *root)
{
    if(!ldr)
        return NULL;
    Dir *vd = ldr->LoadDir(fn, unmangled);
    if(vd)
    {
        std::string parentname = fn;
        StripLastPath(parentname);

        DirBase *parent = parentname.empty() ? root : root->getDir(parentname.c_str(), true);
        //parent->add
        assert(false); // FIXME
    }
    return vd;
}

DirBase *VFSHelper::GetDir(const char* dn, bool create /* = false */)
{
    const char *unmangled = dn;
    std::string fixed = dn; // TODO: get rid of alloc
    FixPath(fixed);
    dn = fixed.c_str();

    if(!merged)
        return NULL;
    if(!*dn)
        return merged;
    Dir *vd = merged->getDir(dn);

    if(!vd && create)
    {
        for(LoaderArray::iterator it = loaders.begin(); it != loaders.end(); ++it)
            if((vd = VFSHelper_GetDirByLoader(*it, dn, unmangled, GetDirRoot())))
                break;

        if(!vd)
            vd = safecastNonNull<InternalDir*>(merged->getDir(dn, true)); // typecast is for debug checking only
    }

    //printf("VFS: GetDir '%s' -> '%s' (%s:%p)\n", dn, vd ? vd->fullname() : "NULL", vd ? vd->getType() : "?", vd);

    return vd;
}

Dir *VFSHelper::GetDirRoot(void)
{
    return merged;
}


void VFSHelper::ClearGarbage(void)
{
    merged->clearGarbage();
}



// DEBUG STUFF


struct _DbgParams
{
    _DbgParams(std::ostream& os_, Dir *parent_, const std::string& sp_)
        : os(os_), parent(parent_), sp(sp_) {}

    std::ostream& os;
    Dir *parent;
    const std::string& sp;
};

static void _DumpFile(File *vf, void *user)
{
    _DbgParams& p = *((_DbgParams*)user);

    p.os << p.sp << "f|" << vf->name() << " [" << vf->getType() << ", ref " << vf->ref.count() << ", 0x" << vf << "]";

    if(strncmp(p.parent->fullname(), vf->fullname(), p.parent->fullnameLen()))
        p.os << " <-- {" << vf->fullname() << "} ***********";

    p.os << std::endl;
}

static void _DumpTreeRecursive(Dir *vd, void *user)
{
    _DbgParams& p = *((_DbgParams*)user);

    std::string sub = p.sp + "  ";

    p.os << p.sp << "d|" << vd->name() << " [" << vd->getType() << ", ref " << vd->ref.count() << ", 0x" << vd << "]";

    if(p.parent && strncmp(p.parent->fullname(), vd->fullname(), strlen(p.parent->fullname())))
        p.os << " <-- {" << vd->fullname() << "} ***********";
    p.os << std::endl;

    _DbgParams recP(p.os, vd, sub);

    vd->forEachDir(_DumpTreeRecursive, &recP);

    vd->forEachFile(_DumpFile, &recP);

}

void VFSHelper::debugDumpTree(std::ostream& os, Dir *start /* = NULL */)
{
    _DbgParams recP(os, NULL, "");
    Dir *d = start ? start : GetDirRoot();
    _DumpTreeRecursive(d, &recP);
}


VFS_NAMESPACE_END

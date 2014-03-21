// Dir.cpp - basic directory interface + classes
// For conditions of distribution and use, see copyright notice in VFS.h

#include <set>

#include "VFSInternal.h"
#include "VFSTools.h"
#include "VFSFile.h"
#include "VFSDir.h"

VFS_NAMESPACE_START

DirBase::DirBase(const char *fullpath)
{
    _setName(fullpath);
}

DirBase::~DirBase()
{
}


File *DirBase::getFile(const char *fn)
{
    while(fn[0] == '.' && fn[1] == '/') // skip ./
        fn += 2;

    char *slashpos = (char *)strchr(fn, '/');

    // if there is a '/' in the string, descend into subdir and continue there
    if(slashpos)
    {
        // "" (the empty directory name) is allowed, so we can't return 'this' when hitting an empty string the first time.
        // This whole mess is required for absolute unix-style paths ("/home/foo/..."),
        // which, to integrate into the tree properly, sit in the root directory's ""-subdir.
        // FIXME: Bad paths (double-slashes and the like) need to be normalized elsewhere, currently!

        size_t len = strlen(fn);
        char *dup = (char*)VFS_STACK_ALLOC(len + 1);
        memcpy(dup, fn, len + 1); // also copy the null-byte
        slashpos = dup + (slashpos - fn); // use direct offset, not to have to recount again the first time
        DirBase *subdir = this;
        const char *ptr = dup;
        Dirs::iterator it;

        goto pos_known;
        do
        {
            ptr = slashpos + 1;
            while(ptr[0] == '.' && ptr[1] == '/') // skip ./
                ptr += 2;
            slashpos = (char *)strchr(ptr, '/');
            if(!slashpos)
                break;

        pos_known:
            *slashpos = 0;
            it = subdir->_subdirs.find(ptr);
            if(it != subdir->_subdirs.end())
                subdir = it->second; // found it
            else
                subdir = NULL; // bail out
        }
        while(subdir);
        VFS_STACK_FREE(dup);
        if(!subdir)
            return NULL;
        // restore pointer into original string, by offset
        ptr = fn + (ptr - dup);
        return subdir->getFileByName(ptr);
    }

    // no subdir? file must be in this dir now.
    return getFileByName(fn);
}


DirBase *DirBase::getDir(const char *subdir, bool forceCreate /* = false */)
{
    if(!subdir[0] || (subdir[0] == '.' && (!subdir[1] || subdir[1] == '/'))) // empty string or "." or "./" ? use this.
        return this;

    DirBase *ret = NULL;
    char *slashpos = (char *)strchr(subdir, '/');

    // if there is a '/' in the string, descend into subdir and continue there
    if(slashpos)
    {
        // from a/b/c, cut out the a, without the trailing '/'.
        const char *sub = slashpos + 1;
        size_t copysize = slashpos - subdir;
        char * const t = (char*)VFS_STACK_ALLOC(copysize + 1);
        memcpy(t, subdir, copysize);
        t[copysize] = 0;
        Dirs::iterator it = _subdirs.find(t);
        if(it != _subdirs.end())
        {
            // TODO: get rid of recursion
            ret = it->second->getDir(sub, forceCreate); // descend into subdirs
        }
        else if(forceCreate)
        {
            // -> newname = fullname() + '/' + t
            size_t fullLen = fullnameLen();
            DirBase *ins;
            if(fullLen)
            {
                char * const newname = (char*)VFS_STACK_ALLOC(fullLen + copysize + 2);
                char *ptr = newname;
                memcpy(ptr, fullname(), fullLen);
                ptr += fullLen;
                *ptr++ = '/';
                memcpy(ptr, t, copysize);
                ptr[copysize] = 0;
                ins = createNew(newname);
                VFS_STACK_FREE(newname);
            }
            else
                ins = createNew(t);

            _subdirs[ins->name()] = ins;
            ret = ins->getDir(sub, true); // create remaining structure
        }
    }
    else
    {
        Dirs::iterator it = _subdirs.find(subdir);
        if(it != _subdirs.end())
            ret = it->second;
        else if(forceCreate)
        {
            size_t fullLen = fullnameLen();
            if(fullLen)
            {
                // -> newname = fullname() + '/' + subdir
                size_t subdirLen = strlen(subdir);
                char * const newname = (char*)VFS_STACK_ALLOC(fullLen + subdirLen + 2);
                char *ptr = newname;
                memcpy(ptr, fullname(), fullLen);
                ptr += fullLen;
                *ptr++ = '/';
                memcpy(ptr, subdir, subdirLen);
                ptr[subdirLen] = 0;

                ret = createNew(newname);
                VFS_STACK_FREE(newname);
            }
            else
            {
                ret = createNew(subdir);
            }

            _subdirs[ret->name()] = ret;
        }
    }

    return ret;
}

static void _iterDirs(const Dirs &m, DirEnumCallback f, void *user)
{
    for(Dirs::const_iterator it = m.begin(); it != m.end(); ++it)
        f(const_cast<DirBase*>(it->second.content()), user);
}

void DirBase::forEachDir(DirEnumCallback f, void *user /* = NULL */, bool safe /* = false */)
{
    if(safe)
    {
        Dirs cp = _subdirs;
        _iterDirs(cp, f, user);
    }
    else
        _iterDirs(_subdirs, f, user);
}



Dir::Dir(const char *fullpath)
: DirBase(fullpath)
{
}

Dir::~Dir()
{
}

File *Dir::getFileByName(const char *fn) const
{
    Files::const_iterator it = _files.find(fn);
    return it == _files.end() ? NULL : it->second;
}

static void _iterFiles(const Files &m, FileEnumCallback f, void *user)
{
    for(Files::const_iterator it = m.begin(); it != m.end(); ++it)
        f(const_cast<File*>(it->second.content()), user);
}

void Dir::forEachFile(FileEnumCallback f, void *user /* = NULL */, bool safe /* = false */)
{
    load();
    if(safe)
    {
        Files cp = _files;
        _iterFiles(cp, f, user);
    }
    else
        _iterFiles(_files, f, user);
}


bool Dir::add(File *f)
{
    if(!f)
        return false;

    Files::iterator it = _files.find(f->name());

    if(it != _files.end())
    {
        File *oldf = it->second;
        if(oldf == f)
            return false;

        _files.erase(it);
    }

    _files[f->name()] = f;
    return true;
}

bool Dir::addRecursive(File *f)
{
    if(!f)
        return false;

    // figure out directory from full file name
    Dir *vdir;
    size_t prefixLen = f->fullnameLen() - f->nameLen();
    if(prefixLen)
    {
        char *dirname = (char*)VFS_STACK_ALLOC(prefixLen);
        --prefixLen; // -1 to strip the trailing '/'. That's the position where to put the terminating null byte.
        memcpy(dirname, f->fullname(), prefixLen); // copy trailing null byte
        dirname[prefixLen] = 0;
        vdir = safecastNonNull<Dir*>(getDir(dirname, true));
        VFS_STACK_FREE(dirname);
    }
    else
        vdir = this;

    return vdir->add(f);
}



// ----- DiskDir start here -----


DiskDir::DiskDir(const char *dir) : Dir(dir)
{
}

DiskDir *DiskDir::createNew(const char *dir) const
{
    return new DiskDir(dir);
}

void DiskDir::load()
{
    _files.clear();
    _subdirs.clear();

    StringList li;
    GetFileList(fullname(), li);
    for(StringList::iterator it = li.begin(); it != li.end(); ++it)
    {
        // TODO: use stack alloc
        std::string tmp = fullname();
        tmp += '/';
        tmp += *it;

        DiskFile *f = new DiskFile(tmp.c_str());
        _files[f->name()] = f;
    }

    li.clear();
    GetDirList(fullname(), li, 0);
    for(StringList::iterator it = li.begin(); it != li.end(); ++it)
    {
        // TODO: use stack alloc
        std::string full(fullname());
        full += '/';
        full += *it; // GetDirList() always returns relative paths

        Dir *d = createNew(full.c_str());
        _subdirs[d->name()] = d;
    }
}

VFS_NAMESPACE_END

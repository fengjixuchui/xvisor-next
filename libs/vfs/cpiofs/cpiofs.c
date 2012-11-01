/**
 * Copyright (c) 2012 Anup Patel.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file cpiofs.c
 * @author Anup Patel. (anup@brainfault.org)
 * @brief CPIO filesystem driver
 *
 * CPIO is well-known archive format. It is widely used by 
 * Linux kernel for setting up contents of its initramfs/initrd.
 *
 * The below code implements a CPIO filesystem driver for read-only
 * purpose since CPIO is an archive format.
 */

#include <vmm_error.h>
#include <vmm_stdio.h>
#include <vmm_modules.h>
#include <libs/stringlib.h>
#include <libs/vfs.h>

#define MODULE_DESC			"CPIO Filesystem Driver"
#define MODULE_AUTHOR			"Anup Patel"
#define MODULE_LICENSE			"GPL"
#define MODULE_IPRIORITY		(VFS_IPRIORITY + 1)
#define	MODULE_INIT			cpiofs_init
#define	MODULE_EXIT			cpiofs_exit

struct cpio_newc_header {
	u8 c_magic[6];
	u8 c_ino[8];
	u8 c_mode[8];
	u8 c_uid[8];
	u8 c_gid[8];
	u8 c_nlink[8];
	u8 c_mtime[8];
	u8 c_filesize[8];
	u8 c_devmajor[8];
	u8 c_devminor[8];
	u8 c_rdevmajor[8];
	u8 c_rdevminor[8];
	u8 c_namesize[8];
	u8 c_check[8];
} __attribute__ ((packed));

/* 
 * Helper routines 
 */

static bool get_next_token(const char *path, const char *perfix, char *result)
{
	char full_path[VFS_MAX_PATH];
	char *p, *q;
	s32 l;

	if (!path || !perfix || !result) {
		return FALSE;
	}

	full_path[0] = '\0';

	if (path[0] != '/') {
		strcpy(full_path, (const char *)("/"));
	}
	strncat(full_path, path, sizeof(full_path));

	l = strlen(perfix);
	if (memcmp(full_path, perfix, l) != 0) {
		return FALSE;
	}

	p = &full_path[l];
	if (*p == '\0') {
		return FALSE;
	}
	if (*p == '/') {
		p++;
	}
	if (*p == '\0') {
		return FALSE;
	}

	q = strchr(p, '/');
	if (q) {
		if (*(q+1) != '\0') {
			return FALSE;
		}
		*q = 0;
	}

	strcpy(result, p);

	return TRUE;
}

static bool check_path(const char *path, const char *prefix, const char *name)
{
	char path1[VFS_MAX_PATH];
	const char *p;
	int l;

	if (!path || !prefix || !name) {
		return FALSE;
	}

	path1[0] = '\0';

	if (path[0] == '/') {
		path++;
	}

	if (prefix[0] == '/') {
		prefix++;
	}

	strncat(path1, prefix, sizeof(path1));

	if (path1[strlen(path1) - 1] != '/') {
		strncat(path1, "/", sizeof(path1));
	}
	strncat(path1, name, sizeof(path1));

	l = strlen(path1);
	if (memcmp(path, path1, l) != 0) {
		return FALSE;
	}

	p = &path[l];
	if (*p == '\0') {
		return TRUE;
	}
	if (*p == '/') {
		p++;
	}
	if (*p == '\0') {
		return TRUE;
	}

	return FALSE;
}

/* 
 * Mount point operations 
 */

static int cpiofs_mount(struct mount *m, const char *dev, u32 flags)
{
	u64 read_count;
	struct cpio_newc_header header;

	if (dev == NULL) {
		return VMM_EINVALID;
	}

	if (vmm_blockdev_total_size(m->m_dev) <= 
				sizeof(struct cpio_newc_header)) {
		return VMM_EFAIL;
	}

	read_count = vmm_blockdev_read(m->m_dev, 
			(u8 *)(&header), 0, sizeof(struct cpio_newc_header));
	if (read_count != sizeof(struct cpio_newc_header)) {
		return VMM_EIO;
	}

	if (strncmp((const char *)header.c_magic, "070701", 6) != 0) {
		return VMM_EINVALID;
	}

	m->m_flags = MOUNT_RDONLY; /* We treat CPIO filesystem as read-only */
	m->m_root->v_data = NULL;
	m->m_data = NULL;

	return VMM_OK;
}

static int cpiofs_unmount(struct mount *m)
{
	m->m_data = NULL;

	return VMM_OK;
}

static int cpiofs_sync(struct mount *m)
{
	/* Not required (read-only filesystem) */
	return VMM_OK;
}

static int cpiofs_vget(struct mount *m, struct vnode *v)
{
	/* Not required */
	return VMM_OK;
}

static int cpiofs_vput(struct mount *m, struct vnode *v)
{
	/* Not required */
	return VMM_OK;
}

/* 
 * Vnode operations 
 */

static int cpiofs_open(struct vnode *v, struct file *f)
{
	/* Not required */
	return VMM_OK;
}

static int cpiofs_close(struct vnode *v, struct file *f)
{
	/* Not required */
	return VMM_OK;
}

static size_t cpiofs_read(struct vnode *v, struct file *f, 
				void *buf, size_t len)
{
	u64 off;
	size_t sz = 0;

	if (v->v_type != VREG) {
		return 0;
	}

	if (f->f_offset >= v->v_size) {
		return 0;
	}

	sz = len;
	if ((v->v_size - f->f_offset) < sz) {
		sz = v->v_size - f->f_offset;
	}

	off = (u64)((u32)(v->v_data));
	sz = vmm_blockdev_read(v->v_mount->m_dev, 
				(u8 *)buf, (off + f->f_offset), sz);

	f->f_offset += sz;

	return sz;
}

static size_t cpiofs_write(struct vnode *v, struct file *f, 
				void *buf, size_t len)
{
	/* Not required (read-only filesystem) */
	return 0;
}

static bool cpiofs_seek(struct vnode *v, struct file *f, loff_t off)
{
	return (off > (loff_t)(v->v_size)) ? FALSE : TRUE;
}

static int cpiofs_fsync(struct vnode *v, struct file *f)
{
	/* Not required (read-only filesystem) */
	return VMM_OK;
}

/* FIXME: */
static int cpiofs_readdir(struct vnode *dv, struct file *f, struct dirent *d)
{
	struct cpio_newc_header header;
	char path[VFS_MAX_PATH];
	char name[VFS_MAX_NAME];
	u32 size, name_size, mode;
	u64 off = 0, rd;
	char buf[9];
	int i = 0;

	if (f->f_offset == 0) {
		d->d_type = DT_DIR;
		strncpy(d->d_name, ".", sizeof(d->d_name));
	} else if (f->f_offset == 1) {
		d->d_type = DT_DIR;
		strncpy(d->d_name, "..", sizeof(d->d_name));
	} else {
		while (1) {
			rd = vmm_blockdev_read(dv->v_mount->m_dev, (u8 *)&header, 
					off, sizeof(struct cpio_newc_header));
			if (!rd) {
				return VMM_EIO;
			}

			if (strncmp((const char *)&header.c_magic, "070701", 6) != 0) {
				return VMM_ENOENT;
			}

			buf[8] = '\0';

			memcpy(buf, &header.c_filesize, 8);
			size = str2uint((const char *)buf, 16);

			memcpy(buf, &header.c_namesize, 8);
			name_size = str2uint((const char *)buf, 16);

			memcpy(buf, &header.c_mode, 8);
			mode = str2uint((const char *)buf, 16);

			rd = vmm_blockdev_read(dv->v_mount->m_dev, (u8 *)path, 
			off + sizeof(struct cpio_newc_header), name_size);
			if (!rd) {
				return VMM_EIO;
			}

			if ((size == 0) && (mode == 0) && (name_size == 11) && 
			    (strncmp(path, "TRAILER!!!", 10) == 0)) {
				return VMM_ENOENT;
			}

			off += sizeof(struct cpio_newc_header); 
			off += (((name_size + 1) & ~3) + 2) + size;
			off = (off + 3) & ~3;

			if (!get_next_token(path, dv->v_path, name)) {
				continue;
			}

			if (i++ == f->f_offset - 2) {
				off = 0;
				break;
			}
		}

		if (mode & 0040000) {
			d->d_type = DT_DIR;
		} else {
			d->d_type = DT_REG;
		}

		strncpy(d->d_name, name, sizeof(name));
	}

	d->d_fileno = (u32)f->f_offset;
	d->d_namlen = (u16)strlen(d->d_name);

	f->f_offset++;

	return 0;
}

static int cpiofs_lookup(struct vnode *dv, char *name, struct vnode *v)
{
	struct cpio_newc_header header;
	char path[VFS_MAX_PATH];
	u32 size, name_size, mode;
	u64 off = 0, rd;
	u8 buf[9];

	while (1) {
		rd = vmm_blockdev_read(dv->v_mount->m_dev, (u8 *)&header, 
					off, sizeof(struct cpio_newc_header));
		if (!rd) {
			return VMM_EIO;
		}

		if (strncmp((const char *)header.c_magic, "070701", 6) != 0) {
			return VMM_ENOENT;
		}

		buf[8] = '\0';

		memcpy(buf, &header.c_filesize, 8);
		size = str2uint((const char *)buf, 16);

		memcpy(buf, &header.c_namesize, 8);
		name_size = str2uint((const char *)buf, 16);

		memcpy(buf, &header.c_mode, 8);
		mode = str2uint((const char *)buf, 16);

		rd = vmm_blockdev_read(dv->v_mount->m_dev, (u8 *)path, 
			off + sizeof(struct cpio_newc_header), name_size);
		if (!rd) {
			return VMM_EIO;
		}

		if ((size == 0) && (mode == 0) && (name_size == 11) && 
		    (strncmp(path, "TRAILER!!!", 10) == 0)) {
			return VMM_ENOENT;
		}

		if (check_path(path, dv->v_path, name)) {
			break;
		}

		off += sizeof(struct cpio_newc_header);
		off += (((name_size + 1) & ~3) + 2) + size;
		off = (off + 3) & ~0x3;
	}

	if ((mode & 00170000) == 0140000) {
		v->v_type = VSOCK;
	} else if((mode & 00170000) == 0120000) {
		v->v_type = VLNK;
	} else if((mode & 00170000) == 0100000) {
		v->v_type = VREG;
	} else if((mode & 00170000) == 0060000) {
		v->v_type = VBLK;
	} else if((mode & 00170000) == 0040000) {
		v->v_type = VDIR;
	} else if((mode & 00170000) == 0020000) {
		v->v_type = VCHR;
	} else if((mode & 00170000) == 0010000) {
		v->v_type = VFIFO;
	} else {
		v->v_type = VREG;
	}

	v->v_mode = 0;
	v->v_mode |= (mode & 00400) ? S_IRUSR : 0;
	v->v_mode |= (mode & 00200) ? S_IWUSR : 0;
	v->v_mode |= (mode & 00100) ? S_IXUSR : 0;
	v->v_mode |= (mode & 00040) ? S_IRGRP : 0;
	v->v_mode |= (mode & 00020) ? S_IWGRP : 0;
	v->v_mode |= (mode & 00010) ? S_IXGRP : 0;
	v->v_mode |= (mode & 00004) ? S_IROTH : 0;
	v->v_mode |= (mode & 00002) ? S_IWOTH : 0;
	v->v_mode |= (mode & 00001) ? S_IXOTH : 0;

	v->v_size = size;
	off += sizeof(struct cpio_newc_header);
	off += (((name_size + 1) & ~3) + 2);
	v->v_data = (void *)((u32)off);

	return 0;
}

static int cpiofs_create(struct vnode *dv, char *filename, u32 mode)
{
	/* Not allowed (read-only filesystem) */
	return VMM_EFAIL;
}

static int cpiofs_remove(struct vnode *dv, struct vnode *v, char *name)
{
	/* Not allowed (read-only filesystem) */
	return VMM_EFAIL;
}

static int cpiofs_rename(struct vnode *dv1, struct vnode *v1, char *sname, 
			struct vnode *dv2, struct vnode *v2, char *dname)
{
	/* Not allowed (read-only filesystem) */
	return VMM_EFAIL;
}

static int cpiofs_mkdir(struct vnode *dv, char *name, u32 mode)
{
	/* Not allowed (read-only filesystem) */
	return VMM_EFAIL;
}

static int cpiofs_rmdir(struct vnode *dv, struct vnode *v, char *name)
{
	/* Not allowed (read-only filesystem) */
	return VMM_EFAIL;
}

static int cpiofs_getattr(struct vnode *v, struct vattr *a)
{
	/* Not allowed (read-only filesystem) */
	return VMM_EFAIL;
}

static int cpiofs_setattr(struct vnode *v, struct vattr *a)
{
	/* Not allowed (read-only filesystem) */
	return VMM_EFAIL;
}

static int cpiofs_truncate(struct vnode *v, loff_t off)
{
	/* Not allowed (read-only filesystem) */
	return VMM_EFAIL;
}

/* cpiofs filesystem */
static struct filesystem cpiofs = {
	.name		= "cpiofs",

	/* Mount point operations */
	.mount		= cpiofs_mount,
	.unmount	= cpiofs_unmount,
	.sync		= cpiofs_sync,
	.vget		= cpiofs_vget,
	.vput		= cpiofs_vput,

	/* Vnode operations */
	.open		= cpiofs_open,
	.close		= cpiofs_close,
	.read		= cpiofs_read,
	.write		= cpiofs_write,
	.seek		= cpiofs_seek,
	.fsync		= cpiofs_fsync,
	.readdir	= cpiofs_readdir,
	.lookup		= cpiofs_lookup,
	.create		= cpiofs_create,
	.remove		= cpiofs_remove,
	.rename		= cpiofs_rename,
	.mkdir		= cpiofs_mkdir,
	.rmdir		= cpiofs_rmdir,
	.getattr	= cpiofs_getattr,
	.setattr	= cpiofs_setattr,
	.truncate	= cpiofs_truncate
};

static int __init cpiofs_init(void)
{
	return vfs_filesystem_register(&cpiofs);
}

static void __exit cpiofs_exit(void)
{
	vfs_filesystem_unregister(&cpiofs);
}

VMM_DECLARE_MODULE(MODULE_DESC,
			MODULE_AUTHOR,
			MODULE_LICENSE,
			MODULE_IPRIORITY,
			MODULE_INIT,
			MODULE_EXIT);

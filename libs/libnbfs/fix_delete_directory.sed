/^int nbfs_delete_directory(/,/^}$/c\
int nbfs_delete_directory(\
    nbfs_context_t *ctx,\
    uint64_t inode_number)\
{\
    nbfs_inode_t inode;\
    nbfs_inode_t parent;\
    uint64_t parent_inode;\
\
    if (!ctx)\
        return -1;\
\
    /* Root directory cannot be deleted. */\
    if (inode_number <= NBFS_ROOT_INODE)\
        return -1;\
\
    /* Read and validate target inode. */\
    if (nbfs_read_inode(\
            ctx,\
            inode_number,\
            &inode) != 0)\
        return -1;\
\
    if (inode.inode_number != inode_number)\
        return -1;\
\
    if (inode.mode != NBFS_MODE_DIRECTORY)\
        return -1;\
\
    /*\
     * Directory must contain only . and ..\
     */\
    if (directory_is_empty(ctx, &inode) != 1)\
        return -1;\
\
    /* Find the parent directory. */\
    if (find_parent_directory(\
            ctx,\
            inode_number,\
            &parent_inode) != 0)\
        return -1;\
\
    if (nbfs_read_inode(\
            ctx,\
            parent_inode,\
            &parent) != 0)\
        return -1;\
\
    if (parent.mode != NBFS_MODE_DIRECTORY)\
        return -1;\
\
    /*\
     * Remove the child entry from the parent directory.\
     *\
     * NBFS directory entries are variable length, so walk\
     * using record_length rather than assuming fixed entries.\
     */\
    {\
        uint8_t block[NBFS_DEFAULT_BLOCK_SIZE];\
        uint64_t offset = 0;\
        int found = 0;\
\
        if (parent.extents[0].block_count == 0)\
            return -1;\
\
        if (nbfs_read_block(\
                ctx,\
                parent.extents[0].start_block,\
                block) != 0)\
            return -1;\
\
        while (offset + sizeof(nbfs_directory_entry_t) <=\
               NBFS_DEFAULT_BLOCK_SIZE)\
        {\
            nbfs_directory_entry_t *entry =\
                (nbfs_directory_entry_t *)(block + offset);\
\
            if (entry->record_length == 0)\
                break;\
\
            if (entry->record_length <\
                sizeof(nbfs_directory_entry_t))\
                return -1;\
\
            if (offset + entry->record_length >\
                NBFS_DEFAULT_BLOCK_SIZE)\
                return -1;\
\
            if (entry->inode == inode_number)\
            {\
                entry->inode = 0;\
                entry->name_length = 0;\
                entry->type = 0;\
\
                if (nbfs_write_block(\
                        ctx,\
                        parent.extents[0].start_block,\
                        block) != 0)\
                    return -1;\
\
                found = 1;\
                break;\
            }\
\
            offset += entry->record_length;\
        }\
\
        if (!found)\
            return -1;\
    }\
\
    /* Release all blocks owned by the directory. */\
    if (file_free_extents(ctx, &inode) != 0)\
        return -1;\
\
    memset(\
        inode.extents,\
        0,\
        sizeof(inode.extents));\
\
    inode.size = 0;\
    inode.links = 0;\
\
    /* Persist the now-free inode contents before releasing it. */\
    if (nbfs_write_inode(\
            ctx,\
            &inode) != 0)\
        return -1;\
\
    /* Release the inode bitmap allocation. */\
    if (nbfs_free_inode(\
            ctx,\
            inode_number) != 0)\
        return -1;\
\
    /* The parent loses one child-directory link. */\
    if (parent.links > 0)\
        parent.links--;\
\
    if (nbfs_write_inode(\
            ctx,\
            &parent) != 0)\
        return -1;\
\
    ctx->dirty = true;\
\
    return 0;\
}

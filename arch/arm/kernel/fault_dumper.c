/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/kmsg_dump.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "fault_dumper.h"

#define FAULT_SOURCE_MEM_COUNT 16
#define TOTAL_FAULT_MEMORIES (FAULT_SOURCE_MEM_COUNT + 1) /* Reserve a slot for SRAM dest aswell*/
#define TOTAL_DUMPER_INFO_PARAMS (TOTAL_FAULT_MEMORIES * 2)
#define DUMPER_INFO_OFFSET_TO_MEMORY_SIZE 1
#define DEST_SLOT 0

#define SLOT_INIT_MEM(x) (x * 2)
#define SLOT_INIT_SIZE(x) (SLOT_INIT_MEM(x) + DUMPER_INFO_OFFSET_TO_MEMORY_SIZE)

struct src_memory
{
	void __iomem *memory_base;
	size_t memory_size;
};

struct callback_handle
{
	struct src_memory memories_used[TOTAL_FAULT_MEMORIES];
	void* tmp_iomem_buf; //we dont have memcpy_iotoio so we need a temp buffer
	size_t total_memory_size;
};

static void fault_dump_persist(struct fault_callback *fault_dumper);

//need to store both the source {addr, size}
static uint dumper_init_info[TOTAL_DUMPER_INFO_PARAMS];
module_param_array(dumper_init_info, uint, NULL, S_IRUSR);
MODULE_PARM_DESC(dumper_init_info, 
	"Information needed to be able to dump in case of abort: \n"
	"sram-base-addr, sram-size (REQUIRED)\n"
	"io-source-addr[], io-source-size[] (OPTIONAL, up to " __stringify(FAULT_SOURCE_MEM_COUNT) " memories) \n");

static int __init fault_dumper_init_module(void)
{
	struct fault_callback* cb;
	struct callback_handle* cb_handle;
	void* tmp_iomem_buf;
	int saved_memories, tmp_buf_size;
	size_t remaining_size;

	if(!dumper_init_info[DEST_SLOT] || !dumper_init_info[DEST_SLOT + DUMPER_INFO_OFFSET_TO_MEMORY_SIZE])
	{
		pr_err("We need a destination to place the dumped logs, otherwise this is useless!\n");
		goto exit_register_dump;
	}

	cb = kcalloc(sizeof(struct fault_callback), sizeof(u8), GFP_KERNEL);
	if(!cb)
		goto exit_register_dump;

	cb->fault_dump_persist = fault_dump_persist;
	
	cb_handle = kcalloc(sizeof(struct callback_handle), sizeof(u8), GFP_KERNEL);
	if(!cb_handle)
		goto exit_cb_handle;

	cb->info.handle = cb_handle;

	cb_handle->memories_used[DEST_SLOT].memory_base = ioremap(dumper_init_info[SLOT_INIT_MEM(DEST_SLOT)], dumper_init_info[SLOT_INIT_SIZE(DEST_SLOT)]);
	cb_handle->memories_used[DEST_SLOT].memory_size = dumper_init_info[SLOT_INIT_SIZE(DEST_SLOT)];
	if(!cb_handle->memories_used[DEST_SLOT].memory_base)
		goto exit_memory_base;

	saved_memories = DEST_SLOT + 1;
	tmp_buf_size = 0;

	while(dumper_init_info[SLOT_INIT_MEM(saved_memories)] && dumper_init_info[SLOT_INIT_SIZE(saved_memories)])
	{
		pr_debug("mem: %x, size: %x\n", dumper_init_info[SLOT_INIT_MEM(saved_memories)], dumper_init_info[SLOT_INIT_SIZE(saved_memories)]);

		cb_handle->memories_used[saved_memories].memory_base = ioremap(dumper_init_info[SLOT_INIT_MEM(saved_memories)], dumper_init_info[SLOT_INIT_SIZE(saved_memories)]);
		cb_handle->memories_used[saved_memories].memory_size = dumper_init_info[SLOT_INIT_SIZE(saved_memories)];
		if(!cb_handle->memories_used[saved_memories].memory_base)
			goto exit_memory_base;

		if(cb_handle->memories_used[saved_memories].memory_size > tmp_buf_size)
		{
			tmp_buf_size = cb_handle->memories_used[saved_memories].memory_size;
		}
		cb_handle->total_memory_size += cb_handle->memories_used[saved_memories].memory_size;
		pr_debug("total: %zu, added: %zu\n", cb_handle->total_memory_size, cb_handle->memories_used[saved_memories].memory_size);

		saved_memories++;

		if(saved_memories >= TOTAL_FAULT_MEMORIES)
			break;
	}

	// entry_type_u contains all different types that contain information from different callsites
	if(cb_handle->total_memory_size > (cb_handle->memories_used[DEST_SLOT].memory_size + sizeof(cb->info.entry_type_u)))
	{
		pr_err("Too small destination address; sram-base-size: %zu, total size: %zu\n", cb_handle->memories_used[DEST_SLOT].memory_size, cb_handle->total_memory_size);
		goto exit_memory_base;
	}
	pr_debug("Total size: %zu, sram size: %zu\n", cb_handle->total_memory_size, cb_handle->memories_used[DEST_SLOT].memory_size);


	if(tmp_buf_size)
	{
		tmp_iomem_buf = kcalloc(tmp_buf_size, sizeof(u8), GFP_KERNEL);
		if(!tmp_iomem_buf)
			goto exit_memory_base;

		cb_handle->tmp_iomem_buf = tmp_iomem_buf;
	}

	saved_memories = DEST_SLOT;

	remaining_size = cb_handle->memories_used[saved_memories].memory_size;

	while(remaining_size && cb_handle->tmp_iomem_buf)
	{
		if(remaining_size > tmp_buf_size)
		{
			memcpy_fromio(cb_handle->tmp_iomem_buf, cb_handle->memories_used[saved_memories].memory_base, tmp_buf_size);
			remaining_size -= tmp_buf_size;
		}
		else 
		{
			memcpy_fromio(cb_handle->tmp_iomem_buf, cb_handle->memories_used[saved_memories].memory_base, remaining_size);
			remaining_size = 0;
		}
	}
	
	saved_memories++;
	while(cb_handle->memories_used[saved_memories].memory_base && cb_handle->tmp_iomem_buf)
	{
		pr_debug("memory: %p, addr: %p\n", cb_handle->memories_used[saved_memories].memory_base, cb_handle->memories_used[saved_memories].memory_size);
		// we sanity check all the sources so that it crashes during registering if they are faulty and not during doAbort
		memcpy_fromio(cb_handle->tmp_iomem_buf, cb_handle->memories_used[saved_memories].memory_base, cb_handle->memories_used[saved_memories].memory_size);
		saved_memories++;
		if(saved_memories >= TOTAL_FAULT_MEMORIES)
			break;
	}

	mutex_init(&cb->info.callback_mutex);
	fault_dump_register(cb);
	return 0;


exit_memory_base:
	while(saved_memories)
	{ 
		--saved_memories; // get idx
		iounmap(cb_handle->memories_used[saved_memories].memory_base);
	}
	kfree(cb_handle);

exit_cb_handle:
	kfree(cb);

exit_register_dump:
	return -EINVAL;
}

static void __exit fault_dumper_cleanup_module(void)
{
	
	int saved_memories;
	struct callback_handle* cb_handle;
	struct fault_callback *current_callback;

	current_callback = get_current_fault_callback();
	if(!current_callback)
		goto fault_dumper_cleanup;
	
	mutex_lock(&current_callback->info.callback_mutex);
	fault_dump_unregister(current_callback);
	mutex_unlock(&current_callback->info.callback_mutex);
	mutex_destroy(&current_callback->info.callback_mutex);

	cb_handle = current_callback->info.handle;
	saved_memories = 0;
	while(cb_handle->memories_used[saved_memories].memory_base)
	{
		iounmap(cb_handle->memories_used[saved_memories].memory_base);
		saved_memories++;
		if(saved_memories >= TOTAL_FAULT_MEMORIES)
			break;
	}

	if(cb_handle->tmp_iomem_buf) //this is optional so check for null
		kfree(cb_handle->tmp_iomem_buf);

	kfree(cb_handle);
	kfree(current_callback);

	pr_debug("Unregistered callback and free:d memories!\n");
	return;

fault_dumper_cleanup:
	pr_err("No callback installed!\n");
	return;
}


#define WRITE_SRAM_AND_MOVE(value, sram_mem)  \
	writel_relaxed(value, sram_mem); \
	sram_mem += sizeof(value);

void* handle_arm_die(struct arm_die_entry* arm_die, void* sram_mem)
{
	size_t str_len;
	
	WRITE_SRAM_AND_MOVE(arm_die->signo, 	sram_mem);
	WRITE_SRAM_AND_MOVE(arm_die->si_code, 	sram_mem);
	WRITE_SRAM_AND_MOVE(arm_die->addr, 		sram_mem);
	WRITE_SRAM_AND_MOVE(arm_die->err, 		sram_mem);
	WRITE_SRAM_AND_MOVE(arm_die->trap, 		sram_mem);

	str_len = strnlen(arm_die->str, MAX_LEN_STR_ARM_DIE - 1);
	memcpy_toio(sram_mem, arm_die->str, str_len);
	((char*)sram_mem)[str_len] = '\0';
	sram_mem += (str_len + sizeof('\0'));
	return sram_mem;
}


void fault_dump_persist(struct fault_callback *fault_dumper)
{
	size_t total_size_needed;
	uint32_t written_bytes, currently_occupied_bytes;
	int saved_memories;
	struct fault_callback_info *info = &fault_dumper->info;
	struct callback_handle* cb_handle = fault_dumper->info.handle;
	void* sram_mem = cb_handle->memories_used[DEST_SLOT].memory_base;
	size_t sram_size = cb_handle->memories_used[DEST_SLOT].memory_size;

	currently_occupied_bytes = readl_relaxed(sram_mem);
	total_size_needed = sizeof(fault_dumper->info.which_entry) + sizeof(written_bytes);
	total_size_needed += fault_dumper->info.len_entry + cb_handle->total_memory_size; //specific callback len and user-specified len

	if(sram_size < (total_size_needed + currently_occupied_bytes))
		goto exit_persist;

	sram_mem += sizeof(currently_occupied_bytes);
	sram_mem += currently_occupied_bytes;

	writel_relaxed(total_size_needed - sizeof(written_bytes), sram_mem);
	sram_mem += sizeof(written_bytes);
	
	writel_relaxed(fault_dumper->info.which_entry, sram_mem);
	sram_mem += sizeof(fault_dumper->info.which_entry);
	
	saved_memories = DEST_SLOT + 1;
	while(cb_handle->memories_used[saved_memories].memory_base)
	{
		pr_debug("memory: %p, addr: %p\n", cb_handle->memories_used[saved_memories].memory_base, cb_handle->memories_used[saved_memories].memory_size);
		memcpy_fromio(cb_handle->tmp_iomem_buf, cb_handle->memories_used[saved_memories].memory_base, cb_handle->memories_used[saved_memories].memory_size);
		memcpy_toio(sram_mem, cb_handle->tmp_iomem_buf, cb_handle->memories_used[saved_memories].memory_size);
		sram_mem += cb_handle->memories_used[saved_memories].memory_size;
		
		saved_memories++;

		if(saved_memories >= TOTAL_FAULT_MEMORIES)
			break;
	}


	switch(info->which_entry)
	{
		case FAULT_ENTRY_ARM_DIE:
			sram_mem = handle_arm_die(&info->entry_type_u.arm_die, sram_mem);
			break;
		default:
			break;
	}

	pr_debug("written_bytes: %u, sram_mem: %p, sram_base: %p, currently_occupied_bytes: %u", total_size_needed, sram_mem, cb_handle->memories_used[DEST_SLOT].memory_base, currently_occupied_bytes);
	writel(currently_occupied_bytes + total_size_needed, cb_handle->memories_used[DEST_SLOT].memory_base);

exit_persist:
	return;
}

static int test_fault_dump_persist(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops test_dump_controller = {
	.set = test_fault_dump_persist,
};
module_param_cb(test_dump_controller_active, &test_dump_controller, NULL, 0644);

static int test_fault_dump_persist(const char *val, const struct kernel_param *kp)
{
	struct fault_callback *current_callback;

	current_callback = get_current_fault_callback();
	if(!current_callback)
		goto test_fallback_err;
	current_callback->info.entry_type_u.arm_die.str = "TESTING";
	current_callback->info.entry_type_u.arm_die.signo = 1;
	current_callback->info.entry_type_u.arm_die.si_code = 2;
	current_callback->info.entry_type_u.arm_die.addr = (void*)0xFAFAFA;
	current_callback->info.entry_type_u.arm_die.err = 3;
	current_callback->info.entry_type_u.arm_die.trap = 4;
	current_callback->info.which_entry = FAULT_ENTRY_ARM_DIE;
	current_callback->info.len_entry = sizeof(current_callback->info.entry_type_u.arm_die);
	current_callback->fault_dump_persist(current_callback);
	return 0;

test_fallback_err:
	pr_err("No callback installed\n");
	return 1;
}


module_init(fault_dumper_init_module);
module_exit(fault_dumper_cleanup_module);
MODULE_LICENSE("GPL");
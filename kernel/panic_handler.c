/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Description:
 * 
 * Module used to save kernel logs to persistent memory upon crash. This is triggered, and the logs are retrieved, through the kmsg_dumper callchain.
 * To use the module the parameter kmsg_dumper_memory has to be set at initialization, this specifies the area of memory to write to as well as the size of the area.
 * 
 * insmod panic_handler.ko kmsg_dumper_memory=<destination-memory>,<size>
 * 
 * Where <destination-memory> is the physical address specifying the start of the requested area, and <size> is the total allowed size of the buffer including header information (sram_log_area).
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

#define PANIC_READ_BUF_SIZE 1024 	// Size of buffer used to read from kmsg_dump_get_buffer()
#define MIN_BUF_SIZE 1024 		 	// Minimum size allowed for buffer (including header)

// Structure of memory in SRAM area
typedef struct {
	bool panic_detected;
	size_t buf_size;
	size_t head;
} sram_buf_header;

typedef struct {
	sram_buf_header header;
	u8 buf[];
} sram_log_area;

static struct panic_handler_context {
	struct kmsg_dumper dump; // Dump function for panic callback
	void *virt_addr;
	phys_addr_t phys_addr;
	size_t size; // SRAM area size

	sram_buf_header local_header;
	sram_log_area *sram_buf; // handle to SRAM area

	char *panic_buf; // Buffer used to read from kmsg_dump_get_buffer()
	size_t panic_buf_sz;
	size_t read_len;

	bool sram_configured;
} panic_handler_ctx;

static bool panic_handler_activated = false;
module_param_hw(panic_handler_activated, bool, other, S_IRUGO);
MODULE_PARM_DESC(panic_handler_activated, "Shows current state of the module, the state is only true if correctly configured");

#define KDM_MIN_SIZE 1024 // Minimum size allowed for kmsg dumper SRAM area
#define KDM_BASE_ADDRESS_SLOT 0
#define KDM_SIZE_SLOT 1
static uint kmsg_dumper_memory[2];
module_param_array(kmsg_dumper_memory, uint, NULL, S_IRUSR);
MODULE_PARM_DESC(kmsg_dumper_memory,
	"Information about the memory area needed for setup of kmsg log dumper: \n"
	"sram-base-addr, sram-size\n"); // kmsg_dumper_memory=[Start of SRAM area as physical address],[Size of area]

/* Writes content of buf to SRAM area and updates header info
*
* @buf: Buffer used as source
* @write_len: Length to write
*
* Returns: true if theres space left in the SRAM buffer, otherwise false
*/
static bool write_to_sram_buf(char* buf, size_t write_len) {
	bool ret = true;
	size_t new_head = 0;
	if (write_len >= panic_handler_ctx.local_header.head) {
		buf = &(buf[write_len - panic_handler_ctx.local_header.head]); 	// Move past oldest bytes of the log
		write_len = panic_handler_ctx.local_header.head;				// Ajust writeable len
		ret = false;
	}
	new_head = panic_handler_ctx.local_header.head - write_len;
	pr_info("panic_callback: Writing %d bytes to SRAM buf at index %d\n", write_len, new_head);
	memcpy_toio(&(panic_handler_ctx.sram_buf->buf[new_head]), buf, write_len);
	panic_handler_ctx.local_header.head = new_head;
	panic_handler_ctx.sram_buf->header = panic_handler_ctx.local_header;
	return ret;
}

/* Callback function used to catch panics, called from callchain and retrieves a handle used to retrieve logs
*
* @dumper: handle to dumper (used to retrievve logs)
* @reason: The kind of fault that triggered the dump
*/
static void panic_handler_dump(struct kmsg_dumper *dumper, enum kmsg_dump_reason reason)
{

	pr_info("panic_handler: Started...\n");
	struct panic_handler_context *ctx = &panic_handler_ctx;
	if (!ctx->sram_configured) {
		pr_info("panic_handler: Not initialised, can not save backtrace.\n");
		return;
	}

	ctx->read_len = 0;
	if (kmsg_dump_get_buffer(dumper, false, ctx->panic_buf, ctx->panic_buf_sz, &(ctx->read_len))) {
		ctx->local_header.panic_detected = true;
		do {
			if(!write_to_sram_buf(ctx->panic_buf, ctx->read_len)) {
				pr_info("panic_callback: End of buffer reached, logs saved\n");
				break;
			}
		} while (kmsg_dump_get_buffer(dumper, false, ctx->panic_buf, ctx->panic_buf_sz, &(ctx->read_len)));
	}
	return;
}

// Writes to ptr and checks the value
bool memIsWritable(void* ptr) {
	u8 original_state;
	memcpy_fromio(&original_state, ptr, sizeof(u8));
	u8 write_compare = 55; // Dummy value
	u8 read_compare = 0;
	memcpy_toio(ptr, &write_compare, sizeof(u8));
	memcpy_fromio(&read_compare, ptr, sizeof(u8));
	memcpy_toio(ptr, &original_state, sizeof(u8));
	return (write_compare == read_compare);
}

/* Attempts to retrieve handle to SRAM area
*
* Returns: True if succsessful, else false
*/
bool panic_handler_get_sram_area(void)
{
	struct panic_handler_context *ctx = &panic_handler_ctx;
	//Module parameters sanity check
	if (kmsg_dumper_memory[KDM_SIZE_SLOT] < MIN_BUF_SIZE) {
		pr_err("kmsg_dumper_memory sram-size is to small, minimum size is %d\n",
		       MIN_BUF_SIZE);
		return false;
	}

	ctx->phys_addr = (phys_addr_t)(kmsg_dumper_memory[KDM_BASE_ADDRESS_SLOT]);
	ctx->size = kmsg_dumper_memory[KDM_SIZE_SLOT];

	ctx->virt_addr = ioremap(ctx->phys_addr, ctx->size);
	if (!ctx->virt_addr) {
		pr_err(KERN_ERR "panic_handler: ioremap failed\n");
		return false;
	}

	ctx->sram_buf = (sram_log_area *)ctx->virt_addr;
	ctx->local_header.panic_detected = false;
	ctx->local_header.buf_size = ctx->size - sizeof(sram_buf_header);
	ctx->local_header.head = ctx->local_header.buf_size - 1;
	ctx->sram_buf->header = ctx->local_header;

	if (!memIsWritable(ctx->virt_addr)) {
		pr_err(KERN_ERR "panic_handler: Memory area is not writable, failed to setup memory\n");
		return false;
	}

	ctx->sram_configured = true;

	pr_info(KERN_ERR "panic_handler: Memory for panicbuffer acquired.\n");
	return true;
}

void free_resources(void)
{
	struct panic_handler_context *ctx = &panic_handler_ctx;
	kmsg_dump_unregister(&ctx->dump);
	kfree(ctx->panic_buf);
	ctx->phys_addr = NULL;
	ctx->virt_addr = NULL;
	ctx->sram_buf = NULL;
	ctx->sram_configured = false;
	panic_handler_activated = false;
	pr_info("panic_handler: Module resources freed and panic callback unregistered.\n");
}

/* Verify that required parameters has been set
*
* Returns: True if all has bee set, else false
*/
bool check_kmsg_dumper_memory(void)
{
	if (!kmsg_dumper_memory[KDM_BASE_ADDRESS_SLOT] ||
	    !kmsg_dumper_memory[KDM_SIZE_SLOT] ||
		kmsg_dumper_memory[KDM_SIZE_SLOT] < KDM_MIN_SIZE) {
		return false;
	}
	return true;
}

/* Setups module with configured values, triggered by setting cb_panic_handler_active parameter
*
* Returns: 0 if sucessful, error if it fails
*/
static int configure_kmsg_dumper(void)
{
	int err = -EINVAL;
	struct panic_handler_context *ctx = &panic_handler_ctx;
	pr_info("panic_handler: Setup and configuration started...\n");

	if (ctx->sram_configured) {
		pr_info("panic_handler: Kmsg dumper allready set up.\n");
		return 0;
	}

	if (!check_kmsg_dumper_memory()) {
		pr_err("The parameter kmsg_dumper_memory has to be set to attempt configuration!\n");
		return err;
	}

	if (!panic_handler_get_sram_area()) {
		return err;
	}

	if (!ctx->panic_buf) { // If not allready allocated
		ctx->panic_buf_sz = PANIC_READ_BUF_SIZE;
		ctx->panic_buf = kmalloc_array(ctx->panic_buf_sz, sizeof(char), GFP_KERNEL);
		if (!ctx->panic_buf) {
			pr_err("panic_handler: Failed to retrieve memory for panic_buf\n");
			return err;
		}
	}

	ctx->dump.dump = panic_handler_dump;
	err = kmsg_dump_register(&ctx->dump);
	if (err) {
		pr_err("panic_handler: registering kmsg dumper failed\n");
		return err;
	}
	pr_info("panic_handler: registering kmsg dumper successful\n");
	panic_handler_activated = true;
	pr_info("panic_handler: Successfully setup module.\n");

	return 0;
}

/* Test of the kmsg dumper functionallity by writing to the sram area
*
* @val: Any (not used by function)
* @kp: NULL
*
* Returns: 0 if sucessful, -1 if it fails
*/
static int test_kmsg_dumper(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops test_kmsg_dumper_controller = {
	.set = test_kmsg_dumper,
};
module_param_cb(cb_test_kmsg_dumper, &test_kmsg_dumper_controller, NULL, 0644);

static int test_kmsg_dumper(const char *val, const struct kernel_param *kp)
{
	struct panic_handler_context *ctx = &panic_handler_ctx;
	char* dumpString = "[TEST] Panic_handler kmsg_dumper [TEST]";
	bool ret = false;
	ctx->local_header.panic_detected = true;
	ret = write_to_sram_buf(dumpString, strlen(dumpString));
	pr_info("panic_handler: Testing of write_to_sram_buf %s.\n", ret ? "OK" : "FAIL");
	return 0;
}

/* Called whan module is loaded, sets starting values and tries to configure
*/
static int __init panic_handler_init(void)
{
	struct panic_handler_context *ctx = &panic_handler_ctx;
	ctx->sram_configured = false;
	return configure_kmsg_dumper();
}

/* Called when module is unloaded
*/
static void __exit panic_handler_exit(void)
{
	struct panic_handler_context *ctx = &panic_handler_ctx;
	if (ctx->sram_configured) {
		free_resources();
	}
	pr_info("panic_handler: Module unloaded\n");
}

// Module should be initialized together with kmsg_dumper_memory, i.e. 'insmod panic_handler.ko kmsg_dumper_memory=[physical address],[SRAM buffer size]'
module_init(panic_handler_init);
module_exit(panic_handler_exit);
MODULE_AUTHOR("Gustav Radbrandt, Hitachi Energy");
MODULE_DESCRIPTION("Saves the last logs generated during a kernel panic to persistent memory");
MODULE_LICENSE("GPL");
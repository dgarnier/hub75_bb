#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO */
#include <linux/init.h>         /* Needed for the macros */

static int __init hello_start(void)
{
    printk(KERN_INFO "Loading hello.ko module...\n");
    printk(KERN_INFO "Hello world\n");
    return 0;
}

static void __exit hello_end(void)
{
    printk(KERN_INFO "Goodbye: hello.ko exiting...\n");
}

module_init(hello_start);
module_exit(hello_end);

MODULE_DESCRIPTION("Example kernel module based on many sources on the internet.");
MODULE_AUTHOR("Ken Keller <kkeller@cabinprograms.com>");
MODULE_LICENSE("GPL");
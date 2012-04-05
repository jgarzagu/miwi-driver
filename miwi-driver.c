#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>

#define DEVICE_NAME "miwi"
#define USER_BUFF_SIZE	128  //Limit it to 4096

/* GPIO pins for the LEDs */
#define BEAGLE_LED_USR0	149
#define BEAGLE_LED_USR1	150

//Me quede en: 
//Ver por que no puedo usar pointers en el char data de Read-> marca (ERROR) (para poder escribir m'as de un caracter)
//Ver para que sirve filp->private_data que se usa en los reads y writes.
//Ver por que usar Kmalloc para los apuntadores que se pasan al user data y usarlo !!IMPORTANTE
//Aprender sobre Device Classes, Libro LDD pag. 385

/*Protoypes*/

static int miwi_init(void);
static void miwi_exit(void);
static int miwi_open(struct inode *, struct file *);
static int miwi_close(struct inode *, struct file *);
static ssize_t miwi_read(struct file *, char *, size_t, loff_t *);
static ssize_t miwi_write(struct file *, const char *, size_t, loff_t *);

/*Global Variables*/

//File operation for the driver
static const struct file_operations miwi_fops = {
	.owner		=	THIS_MODULE,
	.open		=	miwi_open,		//fopen()
	.release 	=	miwi_close,		//fclose()
	.read		=	miwi_read,		//fread()
	.write		=	miwi_write		//fwrite()
};

//Device Structure
struct _miwi_dev {
	dev_t devt;		/*Device Numbers (Major and Minor)*/
	struct cdev cdev;	/*Char device strucuture*/
	struct class *class;
	char *udata;		/*User data buffer*/
} miwi_dev;


/* LOADING AND UNLOADING DEVICE DRIVER OPERATIONS */

//Loading the device driver (insmod)
static int __init miwi_init(void){

	int err;
	miwi_dev.devt = MKDEV(0,0); //Asign Major and Minor numbers; Zero means asaign them dynamically


	printk(KERN_DEBUG "Miwi: init\n");

	/*Register Major number dynamically to use Char Device*/

	//Dynamically register a range of char device numbers(Major, Minor)
	if (alloc_chrdev_region(&miwi_dev.devt, 0, 1, DEVICE_NAME) < 0) {
		printk(KERN_DEBUG "Miwi: can't register device\n");
		return -1;
	}
	
	//Add and initializate the file operations structure with (cdev)
	cdev_init(&miwi_dev.cdev, &miwi_fops);
	miwi_dev.cdev.owner = THIS_MODULE;
	miwi_dev.cdev.ops = &miwi_fops;
	err = cdev_add(&miwi_dev.cdev, miwi_dev.devt, 1);
	if(err){
		printk(KERN_DEBUG "Miwi: cdev_add() failed\n");
		unregister_chrdev_region(miwi_dev.devt, 1);
		return err;
	}
	printk(KERN_DEBUG "Miwi: major number= %d\n", MAJOR(miwi_dev.devt));

	//Create Device Class
	miwi_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
	if (!miwi_dev.class) {
		printk(KERN_DEBUG "Miwi: class_create() failed\n");
		return -1;
	}

	//Create device file in /dev/<device_name> with device_name "miwi" -> (/dev/miwi)
	if (!device_create(miwi_dev.class, NULL, miwi_dev.devt, NULL, DEVICE_NAME)){
		printk(KERN_DEBUG "Miwi: device_create() failed\n");
		class_destroy(miwi_dev.class);
		return -1;
	} //Note: you can put in device_create (...,NULL,"miwi%d",1,...) etc to have more numbers like tty1, tty2, etc.

	//Beagleboard GPIO requests
	if (!gpio_request(BEAGLE_LED_USR1, "")) {
		gpio_direction_output(BEAGLE_LED_USR1, 1);
		gpio_requested = 1;
	}else{
		printk(KERN_DEBUG "Miwi: Fail to request GPIO");
	}
	
	/* OLD WAY OF REGISTERING: FOR THIS YOU NEED TO DO THE /dev/foo file with mknod.
	if (register_chrdev(<some_major_number>,DEVICE_NAME,&hellodr_fops) < 0) {
		printk(KERN_DEBUG "Miwi: Can't register device\n");
		return -1;
	}
	*/

	return 0;
}

//Unloading the device driver (rmmod)
static void __exit miwi_exit(void){

	printk(KERN_DEBUG "Miwi: exit\n");

	device_destroy(miwi_dev.class, miwi_dev.devt); 	//Created with device_create()
	class_destroy(miwi_dev.class);			//Created with class_create()
	cdev_del(&miwi_dev.cdev);			//Created with cdev_add()
	unregister_chrdev_region(miwi_dev.devt, 1);	//Created with alloc_chrdev_region()

	/*
	if (miwi_dev.udata)
		kfree(miwi_dev.udata);			//Created with kmalloc()
	*/

	/* OLD WAY OF UNREGISTERING */
	//unregister_chrdev(<some_major_number>,DEVICE_NAME);	
}


/* FILE OPERATIONS OF THE DRIVER (open, close, read, write) */

static int
miwi_open(struct inode *inode, struct file *file)
{
	int status = 0; 

	printk(KERN_DEBUG "Miwi: open file\n");

	//Allocate user data
	/*
	if (!miwi_dev.udata) {
		miwi_dev.udata = kmalloc(USER_BUFF_SIZE, GFP_KERNEL);
		if (!miwi_dev.udata) 
			status = -ENOMEM;
	}
	*/
	return status;
}

static int
miwi_close(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG "Miwi: close file\n");
	return 0;
}

static ssize_t
miwi_read(struct file *file, char __user *buff, size_t count, loff_t *pos)
{
	int ret;
	miwi_dev.udata="Hello World";

	printk(KERN_DEBUG "Miwi: read\n");
	ret= copy_to_user(buff, miwi_dev.udata, strlen(miwi_dev.udata))?-EFAULT:strlen(miwi_dev.udata);

	//Just when reading from $cat, it continues until return 0 and position is grater than 0.
	if(*pos==0){ //If the postion is at 0 (First time to read)
		*pos+=ret;
		return ret;
	}else{
		return 0;
	}
}

static ssize_t
miwi_write(struct file *file, const char __user *buff, size_t count, loff_t *pos)
{
	int ret;
	char data;
	ret=copy_from_user(&data, buff, 1)? -EFAULT:1; //Copy one byte form user space to kernel space
	printk(KERN_DEBUG "Miwi: Write %c\n", data);

	//TURN ON OR OFF THE LED.
	if(gpio_requested){
		if(data == '0'){
			gpio_set_value(BEAGLE_LED_USR1, 0);
			printk(KERN_DEBUG "Miwi: LED OFF");
		}else if(data == '1'){
			gpio_set_value(BEAGLE_LED_USR1, 1);
			printk(KERN_DEBUG "Miwi: LED ON");
		}		
	}else{
		printk(KERN_DEBUG "Miwi: Write not avilable due to FAIL request");
	}

	return ret;
}


module_init(miwi_init);
module_exit(miwi_exit);


MODULE_VERSION("0.1");
MODULE_ALIAS(DEVICE_NAME);
MODULE_DESCRIPTION("Miwi Driver for Beagleboard");
MODULE_AUTHOR("Jorge Guardado <jorgealbertogarza@gmail.com>");
MODULE_LICENSE("GPL");

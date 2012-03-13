#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#define DEVICE_NAME "Miwi driver"

//Me quede en: 
//Agregar dinamicamente el registro del major number y los FileOperations.
//Aprender como se agrega el MKNOD cada vez que se carga el driver.
//Aprender que se debe de hacer con loff_t *pos en write y read
//Ver por que no puedo usar pointers en el char data de Read-> marca (ERROR) (para poder escribir m'as de un caracter)
//Ver para que sirve filp->private_data que se usa en los reads y writes.

/*Protoypes*/

static int miwi_init(void);
static void miwi_exit(void);
static int miwi_open(struct inode *, struct file *);
static int miwi_close(struct inode *, struct file *);
static ssize_t miwi_read(struct file *, char *, size_t, loff_t *);
static ssize_t miwi_write(struct file *, const char *, size_t, loff_t *);

/*Global Variables*/

//File operation for the driver
static struct file_operations hellodr_fops = {
	.owner		=	THIS_MODULE,
	.open		=	miwi_open,		//fopen()
	.release 	=	miwi_close,		//fclose()
	.read		=	miwi_read,		//fread()
	.write		=	miwi_write		//fwrite()
};


static dev_t hellodr_dev_number; //Device Major number variable
int major_number = 251; //Major number (!EREASE THIS WHEN DO ALLOCATE REGISTER)



//#### LOADING AND UNLOADING DEVICE DRIVER OPERATIONS

//Loading the device driver (insmod)
static int __init miwi_init(void){

	printk(KERN_DEBUG "Miwi: init\n");

	//Register Major number dynamically to use Char Device
	/*
	if (alloc_chrdev_region(&hellodr_dev_number, 0, 1, DEVICE_NAME) < 0) {
		printk(KERN_DEBUG "Can't register device\n");
		return -1;
	}
	*/

	//After this we need to register de device File_Operations (cdev)

	if (register_chrdev(major_number,DEVICE_NAME,&hellodr_fops) < 0) {
		printk(KERN_DEBUG "Miwi: Can't register device\n");
		return -1;
	}

	
	//printk("Hellodr major number : %d\n", MAJOR(hellodr_dev_number));
	return 0;
}

//Unloading the device driver (rmmod)
static void __exit miwi_exit(void){

	printk(KERN_DEBUG "Miwi: exit\n");

	//unregister_chrdev_region(hellodr_dev_number, 1); 	//Dynamicaly exit
	unregister_chrdev(major_number,DEVICE_NAME);		//(!REMOVE THIS)
	
}


//#### FILE OPERATIONS TO THE DRIVER

static int
miwi_open(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG "Miwi: open file\n");
	return 0;
}

static int
miwi_close(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG "Miwi: close file\n");
	return 0;
}

static ssize_t
miwi_read(struct file *file, char *buf, size_t count, loff_t *pos)
{
	int ret;
	char *data="Hello World";

	printk(KERN_DEBUG "Miwi: read\n");
	ret= copy_to_user(buf,data,strlen(data))?-EFAULT:strlen(data);

	//Just when reading from $cat, it continues until return 0 and position is grater than 0.
	if(*pos==0){ //If the postion is at 0 (First time to read)
		*pos+=ret;
		return ret;
	}else{
		return 0;
	}
}

static ssize_t
miwi_write(struct file *file, const char *buf, size_t count, loff_t *pos)
{
	int ret;
	char data;
	ret= copy_from_user(&data,buf,1)? -EFAULT:1; //Copy one byte form user space to kernel space
	printk(KERN_DEBUG "Miwi: Write %c\n", data);
	return ret;
}


module_init(miwi_init);
module_exit(miwi_exit);


MODULE_VERSION("0.1");
MODULE_ALIAS(DEVICE_NAME);
MODULE_DESCRIPTION("Miwi Driver for Beagleboard");
MODULE_AUTHOR("Jorge Guardado <jorgealbertogarza@gmail.com>");
MODULE_LICENSE("GPL");

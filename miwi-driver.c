#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>
#include <linux/spi/spi.h>

#define DEVICE_NAME "miwi"

/* Pointer buffer size */
#define SPI_BUFF_SIZE	16
#define USER_BUFF_SIZE	128	//Max. Limit it to 4096

/* GPIO pins for the LEDs */
#define BEAGLE_LED_USR0	149
#define BEAGLE_LED_USR1	150

/* SPI */
#define SPI_BUS 3		//SPI BUS NUMBER (McSPI#) e.g(McSPI1, McSPI2,..., McSPI4) 
#define SPI_BUS_CS1 1		//SPI CHIP SELECT (CS#) SPI bus can have more than one CS, e.g (CS0, CS1)
#define SPI_BUS_SPEED 1000000	//SPI BUS SPEED in Hertz


//TODO:
//Se va a usar el McSPI3 (Juntar pines 17 y 19)
//Ver por que no puedo usar pointers en el char data de Read-> marca (ERROR) (para poder escribir m'as de un caracter)
//Ver para que sirve filp->private_data que se usa en los reads y writes.
//Ver por que usar Kmalloc para los apuntadores que se pasan al user data y usarlo !!IMPORTANTE
//Aprender sobre Device Classes, Libro LDD pag. 385
//Falta: Agregar los fail1, fail2, en el __init para destruir el cdev, class, etc en caso de que falle.
//Quitar los comentarios con // y ponerlos con /**/
//Ver si u8 o char...
//Hoy: Me quede en agregar los pines en el U-boot y ver si funciona el SPI. (Juntar los cables de SPI_IN y SPI_OUT)
//	Con el cat /dev/miwi hay un OOps error que parece es en la transferencia de apuntadores.

/*Protoypes*/

static int __init miwi_init(void);
static void __exit miwi_exit(void);
static int miwi_open(struct inode *, struct file *);
static int miwi_close(struct inode *, struct file *);
static ssize_t miwi_read(struct file *, char *, size_t, loff_t *);
static ssize_t miwi_write(struct file *, const char *, size_t, loff_t *);
static int miwi_spi_probe(struct spi_device *);
static int miwi_spi_remove(struct spi_device *);
static int __init add_miwi_device_to_bus(void);



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
static struct _miwi_dev {
	dev_t devt;				/*Device Numbers (Major and Minor)*/
	struct cdev cdev;			/*Char device strucuture*/
	struct class *class;			/*Driver class*/
	struct spi_device *miwi_spi_device;	/*SPI device struct to save struct returned by Master SPI driver*/
	struct semaphore spi_sem;		/*Semaphore for SPI */
	char *udata;				/*User data buffer*/
} miwi_dev;

//SPI structure for registering our driver to the "Master SPI Controller driver" -> (omap2_mcspi driver)
static struct spi_driver miwi_spi = {
	.driver = {
		.name =	DEVICE_NAME,
		.owner = THIS_MODULE,
	},
	.probe = miwi_spi_probe,			/*Register miwi_spi_probe() function to Master SPI*/
	.remove = __devexit_p(miwi_spi_remove),		/*Register miwi_spi_remove() function to Master SPI*/
};

//SPI structure to transfer data to the Master SPI
static struct _miwi_data {
	struct spi_message msg;
	struct spi_transfer transfer;
	u8 *tx_buff; 
	u8 *rx_buff;
}miwi_data;

int gpio_requested=0;
u8 random_data=0;

/* LOADING AND UNLOADING DEVICE DRIVER OPERATIONS */

//Loading the device driver (insmod)
static int __init miwi_init(void){

	int err;

	memset(&miwi_data, 0, sizeof(miwi_data));
	sema_init(&miwi_dev.spi_sem, 1);

	/*Register Major number dynamically to use Char Device*/

	printk(KERN_DEBUG "Miwi: init\n");

	miwi_dev.devt = MKDEV(0,0); //Asign Major and Minor numbers; Zero means asaign them dynamically

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
		gpio_direction_output(BEAGLE_LED_USR1, 0);
		gpio_requested = 1;
	}else{
		printk(KERN_DEBUG "Miwi: Fail to request GPIO\n");
	}

	//Init and Register our SPI
	err = spi_register_driver(&miwi_spi);
	if (err < 0) {
		printk(KERN_DEBUG "Miwi: spi_register_driver() failed\n");
		return err;
	}

	err = add_miwi_device_to_bus();
	if (err < 0) {
		printk(KERN_DEBUG "Miwi: add_spike_to_bus() failed\n");
		spi_unregister_driver(&miwi_spi);
		return err;
	}

	//Allocate UnSized Pointer Buffers
	miwi_data.tx_buff = kmalloc(SPI_BUFF_SIZE, GFP_KERNEL | GFP_DMA);
	if (!miwi_data.tx_buff) {
		err = -ENOMEM;
		//goto failX;
	}

	miwi_data.rx_buff = kmalloc(SPI_BUFF_SIZE, GFP_KERNEL | GFP_DMA);
	if (!miwi_data.rx_buff) {
		err = -ENOMEM;
		//goto failX;
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
	
	gpio_free(BEAGLE_LED_USR1);			 //Created with gpio_request()
	spi_unregister_device(miwi_dev.miwi_spi_device); //Created with ?????
	spi_unregister_driver(&miwi_spi); 		 //Created with spi_register_driver()

	device_destroy(miwi_dev.class, miwi_dev.devt); 	 //Created with device_create()
	class_destroy(miwi_dev.class);			 //Created with class_create()
	cdev_del(&miwi_dev.cdev);			 //Created with cdev_add()
	unregister_chrdev_region(miwi_dev.devt, 1);	 //Created with alloc_chrdev_region()

	//Free allocated data
	if (miwi_data.tx_buff)
		kfree(miwi_data.tx_buff);

	if (miwi_data.rx_buff)
		kfree(miwi_data.rx_buff);
	/*
	if (miwi_dev.udata)
		kfree(miwi_dev.udata);			//Created with kmalloc()
	*/

	/* OLD WAY OF UNREGISTERING */
	//unregister_chrdev(<some_major_number>,DEVICE_NAME);	
}

/* SPI FUNCTIONS */

static int __init add_miwi_device_to_bus(void)
{
	struct spi_master *spi_master;
	struct spi_device *spi_device;
	struct device *pdev;
	char buff[64];
	int status = 0;

	spi_master = spi_busnum_to_master(SPI_BUS);
	if (!spi_master) {
		printk(KERN_ALERT "Miwi: spi_busnum_to_master(%d) returned NULL\n", SPI_BUS);
		printk(KERN_ALERT "Miwi: Missing modprobe omap2_mcspi?\n");
		return -1;
	}

	spi_device = spi_alloc_device(spi_master);
	if (!spi_device) {
		put_device(&spi_master->dev);
		printk(KERN_ALERT "Miwi: spi_alloc_device() failed\n");
		return -1;
	}

	spi_device->chip_select = SPI_BUS_CS1;

	/* Check whether this SPI bus.cs is already claimed */
	snprintf(buff, sizeof(buff), "%s.%u", 
			dev_name(&spi_device->master->dev),
			spi_device->chip_select);

	pdev = bus_find_device_by_name(spi_device->dev.bus, NULL, buff);
 	if (pdev) {
		/* We are not going to use this spi_device, so free it */ 
		spi_dev_put(spi_device);

		/* 
		 * There is already a device configured for this bus.cs  
		 * It is okay if it us, otherwise complain and fail.
		 */
		if (pdev->driver && pdev->driver->name && 
				strcmp(DEVICE_NAME, pdev->driver->name)) {
			printk(KERN_ALERT 
				"Miwi: Driver [%s] already registered for %s\n",
				pdev->driver->name, buff);
			status = -1;
		} 
	} else {
		spi_device->max_speed_hz = SPI_BUS_SPEED;
		spi_device->mode = SPI_MODE_0;
		spi_device->bits_per_word = 8;
		spi_device->irq = -1;
		spi_device->controller_state = NULL;
		spi_device->controller_data = NULL;
		strlcpy(spi_device->modalias, DEVICE_NAME, SPI_NAME_SIZE);

		status = spi_add_device(spi_device);		
		if (status < 0) {	
			spi_dev_put(spi_device);
			printk(KERN_ALERT "Miwi: spi_add_device() failed: %d\n", 
				status);		
		}				
	}

	put_device(&spi_master->dev);

	return status;
}

static int miwi_spi_transfer_message(void)
{
	int status;

	if (down_interruptible(&miwi_dev.spi_sem))
		return -ERESTARTSYS;

	if (!miwi_dev.miwi_spi_device) {
		up(&miwi_dev.spi_sem);
		return -ENODEV;
	}

	/* Init SPI sata to transfer */	
	spi_message_init(&miwi_data.msg);

	/* put some changing values in tx_buff for testing */	
	miwi_data.tx_buff[0] = random_data++;
	miwi_data.tx_buff[1] = random_data++;
	miwi_data.tx_buff[2] = random_data++;
	miwi_data.tx_buff[3] = random_data++;

	memset(miwi_data.rx_buff, 0, SPI_BUFF_SIZE);

	miwi_data.transfer.tx_buf = miwi_data.tx_buff;
	miwi_data.transfer.rx_buf = miwi_data.rx_buff;
	miwi_data.transfer.len = 4;

	spi_message_add_tail(&miwi_data.transfer, &miwi_data.msg);

	/* Finally send the data to Master SPI driver. It will wait until completion*/
	status = spi_sync(miwi_dev.miwi_spi_device, &miwi_data.msg);

	up(&miwi_dev.spi_sem);

	return status;	
}

static int miwi_spi_probe(struct spi_device *spi_device)
{
	if (down_interruptible(&miwi_dev.spi_sem))
		return -EBUSY;

	/*Save the structure handler returned by Master SPI*/
	miwi_dev.miwi_spi_device = spi_device;
	
	/*Check dmesg to see if the registration was correct*/
	printk("Miwi: SPI ready on bus_num = %d, CS = %d\n", 
		miwi_dev.miwi_spi_device->master->bus_num, 
		miwi_dev.miwi_spi_device->chip_select);


	up(&miwi_dev.spi_sem);

return 0;
}

static int miwi_spi_remove(struct spi_device *spi_device)
{

	if (down_interruptible(&miwi_dev.spi_sem))
		return -EBUSY;
	
	printk("Miwi: SPI bus removed\n");
	miwi_dev.miwi_spi_device = NULL;

	up(&miwi_dev.spi_sem);

return 0;
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

	printk(KERN_DEBUG "Miwi: read\n");

	/* SPI Write and Read */
	ret= miwi_spi_transfer_message();

	if (ret) {
		sprintf(miwi_dev.udata, 
			"spike_do_one_message failed : %d\n",
			ret);
	}
	else {
		sprintf(miwi_dev.udata, 
			"Status: %d\nTX: %d %d %d %d\nRX: %d %d %d %d\n",
			miwi_data.msg.status,
			miwi_data.tx_buff[0], miwi_data.tx_buff[1], 
			miwi_data.tx_buff[2], miwi_data.tx_buff[3],
			miwi_data.rx_buff[0], miwi_data.rx_buff[1], 
			miwi_data.rx_buff[2], miwi_data.rx_buff[3]);
	}

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


MODULE_ALIAS(DEVICE_NAME);
MODULE_AUTHOR("Jorge Guardado <jorgealbertogarza@gmail.com>");
MODULE_DESCRIPTION("Miwi Driver for Beagleboard");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");


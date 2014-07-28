/* eth_q_init.c - eth_q_init, eth_q_phy_read, eth_q_phy_write */

#include <xinu.h>

struct	ether ethertab[1];

/*------------------------------------------------------------------------
 * eth_q_phy_read - read a PHY register
 *------------------------------------------------------------------------
 */
uint16	eth_q_phy_read	(
			volatile struct eth_q_csreg *csrptr,
			uint32	regnum
			)
{
	uint32	retries; /* No. of retries for read */

	/* Wait for the MII to be ready */
	while(csrptr->gmiiar & ETH_QUARK_GMIIAR_GB);

	/* Prepare the GMII address register for read transaction */
	csrptr->gmiiar = 
		(1 << 11)		| /* Physical Layer Address = 1	*/
		(regnum << 6)		| /* PHY Register Number	*/
		(ETH_QUARK_GMIIAR_CR)	| /* GMII Clock Range 100-150MHz*/
		(ETH_QUARK_GMIIAR_GB);	  /* Start the transaction	*/

	/* Wait for the transaction to complete */
	retries = 0;
	while(csrptr->gmiiar & ETH_QUARK_GMIIAR_GB) {
		delay(ETH_QUARK_INIT_DELAY);
		if((++retries) > ETH_QUARK_MAX_RETRIES)
			return 0;
	}

	/* Transaction is complete, read the PHY	*/
	/* register value from GMII data register 	*/
	return (uint16)csrptr->gmiidr;
}

/*------------------------------------------------------------------------
 * eth_q_phy_write - write a PHY register
 *------------------------------------------------------------------------
 */
void	eth_q_phy_write	(
			volatile struct eth_q_csreg *csrptr,
			uint32	regnum,
			uint16	value
			)
{
	uint32	retries; /* No. of retries for write */

	/* Wait for the MII to be ready */
	while(csrptr->gmiiar & ETH_QUARK_GMIIAR_GB);

	/* Write the value to be written in PHY	*/
	/* register in GMII data register	*/
	csrptr->gmiidr = (uint32)value;

	/* Prepare the GMII address register for write transaction */
	csrptr->gmiiar = 
		(1 << 11)		| /* Physical Layer Address = 1	*/
		(regnum << 6)		| /* PHY Register Number	*/
		(ETH_QUARK_GMIIAR_CR)	| /* GMII Clock Range 100-150MHz*/
		(ETH_QUARK_GMIIAR_GW)	| /* Write transaction		*/
		(ETH_QUARK_GMIIAR_GB);	  /* Start the transaction	*/

	/* Wait till the transaction is complete */
	retries = 0;
	while(csrptr->gmiiar & ETH_QUARK_GMIIAR_GB) {
		delay(ETH_QUARK_INIT_DELAY);
		if((++retries) > ETH_QUARK_MAX_RETRIES)
			return;
	}
}

/*------------------------------------------------------------------------
 * eth_q_phy_reset - reset Ethernet PHY
 *------------------------------------------------------------------------
 */
int32	eth_q_phy_reset	(
			volatile struct eth_q_csreg *csrptr
			)
{
	uint16 	value; 	/* Variable to read in PHY registers 	*/
	uint32	retries;/* No.  of retries for reset 		*/

	/* Read the PHY control register (register 0) */
	value = eth_q_phy_read(csrptr, 0);

	/* Set bit 15 in control register to reset the PHY */
	eth_q_phy_write(csrptr, 0, (value | 0x8000));

	/* Wait for PHY reset process to complete */
	retries = 0;
	while(eth_q_phy_read(csrptr, 0) & 0x8000) {
		delay(ETH_QUARK_INIT_DELAY);
		if((++retries) > ETH_QUARK_MAX_RETRIES)
			return SYSERR;
	}

	/* Check if the PHY has auto-negotiation capability */
	value = eth_q_phy_read(csrptr, 1); /* PHY Status register */
	if(value & 0x0008) { /* Auto-negotiation capability present */

		/* Wait for the auto-negotiation process to complete */
		while((eth_q_phy_read(csrptr, 1) & 0x0020) == 0);

		/* Wait for the Link to be Up */
		while((eth_q_phy_read(csrptr, 1) & 0x0004) == 0);
	}
	else { /* Auto-negotiation capability not present */
		/* TODO Set Link speed = 100Mbps */
	}

	kprintf("Ethernet Link is Up\n");

	return OK;
}

/*------------------------------------------------------------------------
 * eth_q_init - initialize the Intel Quark Ethernet device
 *------------------------------------------------------------------------
 */
int32	eth_q_init	(
			struct	dentry *devptr
			)
{
	struct	ether *ethptr;	/* Ethertab entry pointer	*/
	volatile struct eth_q_csreg *csrptr;/* Pointer to Ethernet CSRs */
	struct	eth_q_tx_desc *tx_descs;	/* Array of tx descs	*/
	struct	eth_q_rx_desc *rx_descs;	/* Array of rx descs	*/
	struct	netpacket *pktptr;		/* Pointer to packet	*/
	void	*temptr;			/* Temp. pointer	*/
	uint32	retries; 	/* No. of retries for reset 	*/
	int32	i;

	ethptr = &ethertab[devptr->dvminor];

	ethptr->csr = (struct eth_q_csreg *)devptr->dvcsr;
	csrptr = (struct eth_q_csreg *)ethptr->csr;

	/* Enable CSR Memory Space, Enable Bus Master */
	pci_write_config_word(ethptr->pcidev, 0x4, 0x0006);

	/* Reset the Ethernet MAC */
	csrptr->bmr |= 0x00000001;

	/* Wait for the MAC Reset process to complete */
	retries = 0;
	while(csrptr->bmr & 0x00000001) {
		delay(ETH_QUARK_INIT_DELAY);
		if((++retries) > ETH_QUARK_MAX_RETRIES)
			return SYSERR;
	}

	/* Fixed burst mode */
	csrptr->bmr |= 0x00010000;

	csrptr->omr |= 0x00200000;

	/* Reset the Ethernet PHY */
	eth_q_phy_reset(csrptr);

	/* Set the interrupt handler */
	set_evec(devptr->dvirq, (uint32)devptr->dvintr);

	/* Set the MAC Speed = 100Mbps, Full Duplex mode */
	csrptr->maccr |= (ETH_QUARK_MACCR_RMIISPD100 |
			  ETH_QUARK_MACCR_DM);
	csrptr->maccr |= (0x30000000);
	
	/* Reset the MMC Counters */
	csrptr->mmccr |= ETH_QUARK_MMC_CNTFREEZ | ETH_QUARK_MMC_CNTRST;
	
	/* Retrieve the MAC address from SPI flash */
	get_quark_pdat_entry_data_by_id(QUARK_MAC1_ID,
			(char*)(ethptr->devAddress), ETH_ADDR_LEN);
	
	kprintf("MAC address is %02x:%02x:%02x:%02x:%02x:%02x\n",
		0xff&ethptr->devAddress[0],
		0xff&ethptr->devAddress[1],
		0xff&ethptr->devAddress[2],
		0xff&ethptr->devAddress[3],
		0xff&ethptr->devAddress[4],
		0xff&ethptr->devAddress[5]);

	csrptr->macaddr0l = (uint32)(*((uint32 *)ethptr->devAddress));
	csrptr->macaddr0h = ((uint32)
		(*((uint16 *)(ethptr->devAddress + 4))) | 0x80000000);

	ethptr->txRingSize = ETH_QUARK_TX_RING_SIZE;

	/* Allocate memory for the transmit ring */
	temptr = (void *)getmem(sizeof(struct eth_q_tx_desc) *
					(ethptr->txRingSize+1));
	if((int)temptr == SYSERR) {
		return SYSERR;
	}
	memset(temptr, 0, sizeof(struct eth_q_tx_desc) *
					(ethptr->txRingSize+1));

	/* The transmit descriptors need to be 4-byte aligned */
	ethptr->txRing = (void *)(((uint32)temptr + 3) & (~3));

	/* Allocate memory for transmit buffers */
	ethptr->txBufs = (void *)getmem(sizeof(struct netpacket) *
					(ethptr->txRingSize+1));
	if((int)ethptr->txBufs == SYSERR) {
		return SYSERR;
	}
	ethptr->txBufs = (void *)(((uint32)ethptr->txBufs + 3) & (~3));

	/* Pointers to initialize transmit descriptors */
	tx_descs = (struct eth_q_tx_desc *)ethptr->txRing;
	pktptr = (struct netpacket *)ethptr->txBufs;

	/* Initialize the transmit descriptors */
	for(i = 0; i < ethptr->txRingSize; i++) {
		tx_descs[i].buffer1 = (uint32)(pktptr + i);
	}

	/* Create the output synchronization semaphore */
	ethptr->osem = semcreate(ethptr->txRingSize);
	if((int)ethptr->osem == SYSERR) {
		return SYSERR;
	}

	ethptr->rxRingSize = ETH_QUARK_RX_RING_SIZE;

	/* Allocate memory for the receive descriptors */
	temptr = (void *)getmem(sizeof(struct eth_q_rx_desc) *
					(ethptr->rxRingSize+1));
	if((int)temptr == SYSERR) {
		return SYSERR;
	}
	memset(temptr, 0, sizeof(struct eth_q_rx_desc) *
					(ethptr->rxRingSize+1));

	/* Receive descriptors must be 4-byte aligned */
	ethptr->rxRing = (struct eth_q_rx_desc *)
					(((uint32)temptr + 3) & (~3));

	/* Allocate memory for the receive buffers */
	ethptr->rxBufs = (void *)getmem(sizeof(struct netpacket) *
						(ethptr->rxRingSize+1));
	if((int)ethptr->rxBufs == SYSERR) {
		return SYSERR;
	}

	/* Receive buffers must be 4-byte aligned */
	ethptr->rxBufs = (void *)(((uint32)ethptr->rxBufs + 3) & (~3));

	/* Pointer to initialize receive descriptors */
	rx_descs = (struct eth_q_rx_desc *)ethptr->rxRing;

	/* Pointer to data buffers */
	pktptr = (struct netpacket *)ethptr->rxBufs;

	/* Initialize the receive descriptors */
	for(i = 0; i < ethptr->rxRingSize; i++) {

		rx_descs[i].status   = ETH_QUARK_RDST_OWN;
		rx_descs[i].buf1size = (uint32)sizeof(struct netpacket);
		rx_descs[i].buffer1  = (uint32)(pktptr + i);
	}

	/* Indicate end of ring on last descriptor */
	rx_descs[ethptr->rxRingSize-1].buf1size |= (ETH_QUARK_RDCTL1_RER);

	/* Create the input synchronization semaphore */
	ethptr->isem = semcreate(0);
	if((int)ethptr->isem == SYSERR) {
		return SYSERR;
	}

	/* Enable the Transmit and Receive Interrupts */
	csrptr->ier = (	ETH_QUARK_IER_NIE |
			ETH_QUARK_IER_TIE |
			ETH_QUARK_IER_RIE );

	/* Initialize the transmit descriptor base address */
	csrptr->tdla = (uint32)ethptr->txRing;

	/* Initialize the receive descriptor base address */
	csrptr->rdla = (uint32)ethptr->rxRing;

	/* Enable the MAC Receiver and Transmitter */
	csrptr->maccr |= (ETH_QUARK_MACCR_TE | ETH_QUARK_MACCR_RE);

	/* Start the Transmit and Receive Processes in the DMA */
	csrptr->omr |= (ETH_QUARK_OMR_ST | ETH_QUARK_OMR_SR);

	return OK;

}

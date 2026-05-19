void disk_init(disk_t* disk)
{
    // Step 1: check AHCI controller presence (PCI scan)
    if (pci_find_class(0x01, 0x06)) // SATA AHCI class
    {
        hba_mem_t* abar = ahci_map_bar();
        ahci_attach(disk, abar, 0);
        return;
    }

    // fallback ATA
    ata_attach(disk);
}

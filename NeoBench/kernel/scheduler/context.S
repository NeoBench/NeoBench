    .text
    .global context_switch

context_switch:
    /* TODO:
     * Save registers
     * Restore next task registers
     * Return to new task
     */
    rts

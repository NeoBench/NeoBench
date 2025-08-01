/**
 * NeoBench MOD Music Player
 *
 * This module provides a ProTracker-compatible MOD player for the NeoBench ROM,
 * emulating the Amiga Paula audio chip's capabilities. It supports 4-channel
 * module playback with standard effects.
 */

#include <stdint.h>
#include <string.h>

// ProTracker MOD format constants
#define MOD_MAX_CHANNELS   4      // Original Amiga MOD had 4 channels
#define MOD_MAX_SAMPLES    31     // Maximum samples in MOD file
#define MOD_MAX_PATTERNS   128    // Maximum patterns in MOD file
#define MOD_ROWS_PER_PATTERN 64   // Rows per pattern
#define MOD_SAMPLE_RATE    16726  // Paula hardware sampling rate (PAL)
#define MOD_DEFAULT_BPM    125    // Default BPM

// MOD file header structure
typedef struct {
    char title[20];              // Song title
    struct {
        char name[22];           // Sample name
        uint16_t length;         // Length in words
        uint8_t finetune;        // Finetune value (0-15)
        uint8_t volume;          // Default volume (0-64)
        uint16_t repeat_offset;  // Repeat start point in words
        uint16_t repeat_length;  // Repeat length in words
    } sample[MOD_MAX_SAMPLES];   // Sample information
    uint8_t song_length;         // Number of positions in song
    uint8_t restart_pos;         // Restart position
    uint8_t pattern_table[128];  // Pattern table
    char identifier[4];          // Format identifier
} MOD_Header;

// MOD note structure (internal)
typedef struct {
    uint16_t period;            // Note period
    uint8_t sample_num;         // Sample number (1-31)
    uint8_t effect;             // Effect number
    uint8_t effect_param;       // Effect parameter
} MOD_Note;

// MOD channel state structure
typedef struct {
    uint16_t period;            // Current period
    uint16_t target_period;     // Portamento target period
    uint8_t sample_num;         // Current sample
    uint8_t volume;             // Current volume (0-64)
    uint32_t sample_pos;        // Current position in sample data
    uint32_t sample_inc;        // Sample position increment per tick
    uint16_t repeat_start;      // Sample repeat start
    uint16_t repeat_end;        // Sample repeat end
    uint8_t panning;            // Channel panning (0=left, 128=center, 255=right)
    
    // Effect state
    uint8_t current_effect;     // Current effect
    uint8_t effect_param;       // Current effect parameter
    uint8_t vibrato_pos;        // Vibrato position
    uint8_t vibrato_depth;      // Vibrato depth
    uint8_t tremolo_pos;        // Tremolo position
    uint8_t tremolo_depth;      // Tremolo depth
    uint8_t arpeggio[3];        // Arpeggio notes
    uint8_t arpeggio_pos;       // Arpeggio position
    uint8_t retrigger_count;    // Retrigger counter
    uint8_t tremor_on;          // Tremor on/off flag
    uint8_t tremor_count;       // Tremor counter
    uint8_t glissando;          // Glissando flag
} MOD_Channel;

// MOD player state
typedef struct {
    MOD_Header header;             // MOD file header
    uint8_t *pattern_data;         // Pattern data
    uint8_t *sample_data[MOD_MAX_SAMPLES]; // Sample data pointers
    
    MOD_Channel channels[MOD_MAX_CHANNELS]; // Channel states
    
    uint16_t current_pattern;      // Current pattern
    uint16_t current_row;          // Current row in pattern
    uint16_t current_tick;         // Current tick in row
    uint16_t ticks_per_row;        // Ticks per row (speed)
    uint16_t bpm;                  // Current BPM
    
    uint8_t playing;               // Playing flag
    uint8_t pattern_delay;         // Pattern delay counter
    uint8_t pattern_loop_row;      // Pattern loop start row
    uint8_t pattern_loop_count;    // Pattern loop counter
    
    uint32_t sample_counter;       // Sample counter for timing
    uint32_t samples_per_tick;     // Samples per tick
} MOD_Player;

// Period table (Amiga fine-tuned periods)
// 5 octaves, 12 semitones per octave, 16 fine-tune steps
static const uint16_t period_table[5*12*16] = {
    // Note frequencies for finetune 0
    856,808,762,720,678,640,604,570,538,508,480,453,  // C-1 to B-1
    428,404,381,360,339,320,302,285,269,254,240,226,  // C-2 to B-2
    214,202,190,180,170,160,151,143,135,127,120,113,  // C-3 to B-3
    107,101, 95, 90, 85, 80, 75, 71, 67, 63, 60, 56,  // C-4 to B-4
     53, 50, 47, 45, 42, 40, 37, 35, 33, 31, 30, 28,  // C-5 to B-5
    // ...finetune tables 1-15 would follow
};

// Global MOD player instance
static MOD_Player mod_player;

// Function prototypes
void mod_init(void);
int mod_load(const uint8_t *mod_data);
void mod_play(void);
void mod_stop(void);
void mod_pause(void);
void mod_resume(void);
void mod_set_position(uint8_t position);
void mod_update(void);
void mod_update_tick(void);
void mod_update_row(void);
void mod_update_effects(void);
void mod_update_audio(int16_t *buffer, uint32_t len);
uint16_t mod_get_note_period(uint8_t note, uint8_t sample_num);
void mod_trigger_note(uint8_t channel, uint16_t period, uint8_t sample_num);
void mod_handle_effect(uint8_t channel, uint8_t effect, uint8_t param);

/**
 * Initialize the MOD player
 */
void mod_init(void) {
    // Clear MOD player state
    memset(&mod_player, 0, sizeof(MOD_Player));
    
    // Set default values
    mod_player.ticks_per_row = 6;
    mod_player.bpm = MOD_DEFAULT_BPM;
    mod_player.playing = 0;
    
    // Initialize audio channels
    for (int i = 0; i < MOD_MAX_CHANNELS; i++) {
        mod_player.channels[i].volume = 64;         // Max volume
        mod_player.channels[i].panning = 128;       // Center panning
    }
    
    // Standard Amiga panning (if using stereo output)
    mod_player.channels[0].panning = 32;   // Channel 1 - Left
    mod_player.channels[1].panning = 224;  // Channel 2 - Right
    mod_player.channels[2].panning = 32;   // Channel 3 - Left
    mod_player.channels[3].panning = 224;  // Channel 4 - Right
    
    // Calculate samples per tick
    mod_player.samples_per_tick = (MOD_SAMPLE_RATE * 60) / (mod_player.bpm * 4);
}

/**
 * Load a MOD file from memory
 * Returns 1 on success, 0 on failure
 */
int mod_load(const uint8_t *mod_data) {
    if (!mod_data) {
        return 0;
    }
    
    // Stop any current playback
    mod_stop();
    
    // Copy header data
    memcpy(&mod_player.header, mod_data, sizeof(MOD_Header));
    
    // Verify MOD format
    if (strncmp(mod_player.header.identifier, "M.K.", 4) != 0 &&
        strncmp(mod_player.header.identifier, "4CHN", 4) != 0) {
        // Unsupported format
        return 0;
    }
    
    // Find highest pattern number used
    uint8_t max_pattern = 0;
    for (int i = 0; i < mod_player.header.song_length; i++) {
        if (mod_player.header.pattern_table[i] > max_pattern) {
            max_pattern = mod_player.header.pattern_table[i];
        }
    }
    
    // Point to pattern data in the MOD file
    mod_player.pattern_data = (uint8_t*)(mod_data + sizeof(MOD_Header));
    
    // Point to sample data in the MOD file
    uint32_t offset = sizeof(MOD_Header) + (max_pattern + 1) * MOD_ROWS_PER_PATTERN * 4 * 4;
    for (int i = 0; i < MOD_MAX_SAMPLES; i++) {
        if (mod_player.header.sample[i].length > 0) {
            mod_player.sample_data[i] = (uint8_t*)(mod_data + offset);
            offset += mod_player.header.sample[i].length * 2;
        } else {
            mod_player.sample_data[i] = NULL;
        }
    }
    
    // Reset player state
    mod_player.current_pattern = 0;
    mod_player.current_row = 0;
    mod_player.current_tick = 0;
    mod_player.ticks_per_row = 6;  // Default ticks per row
    mod_player.bpm = MOD_DEFAULT_BPM;
    mod_player.playing = 0;
    mod_player.pattern_delay = 0;
    mod_player.pattern_loop_row = 0;
    mod_player.pattern_loop_count = 0;
    
    // Reset channel states
    for (int i = 0; i < MOD_MAX_CHANNELS; i++) {
        memset(&mod_player.channels[i], 0, sizeof(MOD_Channel));
        mod_player.channels[i].volume = 64;  // Default volume
    }
    
    // Calculate samples per tick
    mod_player.samples_per_tick = (MOD_SAMPLE_RATE * 60) / (mod_player.bpm * 4);
    
    return 1;
}

/**
 * Start playing the loaded MOD
 */
void mod_play(void) {
    if (!mod_player.pattern_data) {
        return;  // No MOD loaded
    }
    
    mod_player.playing = 1;
}

/**
 * Stop playing
 */
void mod_stop(void) {
    mod_player.playing = 0;
    mod_player.current_pattern = 0;
    mod_player.current_row = 0;
    mod_player.current_tick = 0;
    
    // Reset all channels
    for (int i = 0; i < MOD_MAX_CHANNELS; i++) {
        mod_player.channels[i].period = 0;
        mod_player.channels[i].sample_pos = 0;
    }
}

/**
 * Pause playback
 */
void mod_pause(void) {
    mod_player.playing = 0;
}

/**
 * Resume playback
 */
void mod_resume(void) {
    mod_player.playing = 1;
}

/**
 * Set playback position
 */
void mod_set_position(uint8_t position) {
    if (position >= mod_player.header.song_length) {
        position = 0;
    }
    
    mod_player.current_pattern = mod_player.header.pattern_table[position];
    mod_player.current_row = 0;
    mod_player.current_tick = 0;
}

/**
 * Update the MOD player
 */
void mod_update(void) {
    if (!mod_player.playing) {
        return;
    }
    
    // Update current tick
    if (++mod_player.current_tick >= mod_player.ticks_per_row) {
        mod_player.current_tick = 0;
        mod_update_row();
    } else {
        mod_update_tick();
    }
    
    // Update effects
    mod_update_effects();
}

/**
 * Update for a new tick (not the first tick of a row)
 */
void mod_update_tick(void) {
    // Process tick effects for all channels
    for (int i = 0; i < MOD_MAX_CHANNELS; i++) {
        MOD_Channel *chan = &mod_player.channels[i];
        
        // Process tick-based effects
        switch (chan->current_effect) {
            case 0x0: // Arpeggio
                if (chan->effect_param != 0) {
                    chan->arpeggio_pos = (chan->arpeggio_pos + 1) % 3;
                }
                break;
                
            case 0x1: // Portamento up
                chan->period -= chan->effect_param;
                if (chan->period < 113) chan->period = 113; // Highest note
                break;
                
            case 0x2: // Portamento down
                chan->period += chan->effect_param;
                if (chan->period > 856) chan->period = 856; // Lowest note
                break;
                
            case 0x3: // Tone portamento
                if (chan->period < chan->target_period) {
                    chan->period += chan->effect_param;
                    if (chan->period > chan->target_period)
                        chan->period = chan->target_period;
                } else if (chan->period > chan->target_period) {
                    chan->period -= chan->effect_param;
                    if (chan->period < chan->target_period)
                        chan->period = chan->target_period;
                }
                break;
                
            case 0x4: // Vibrato
                // Vibrato processing would go here
                break;
                
            case 0x5: // Tone portamento + Volume slide
                // Combined effect: Tone portamento + Volume slide
                if (chan->period < chan->target_period) {
                    chan->period += chan->effect_param;
                    if (chan->period > chan->target_period)
                        chan->period = chan->target_period;
                } else if (chan->period > chan->target_period) {
                    chan->period -= chan->effect_param;
                    if (chan->period < chan->target_period)
                        chan->period = chan->target_period;
                }
                
                // Volume slide part
                if ((chan->effect_param & 0xF0) != 0) {
                    // Slide up
                    chan->volume += (chan->effect_param >> 4);
                    if (chan->volume > 64) chan->volume = 64;
                } else if ((chan->effect_param & 0x0F) != 0) {
                    // Slide down
                    chan->volume -= (chan->effect_param & 0x0F);
                    if (chan->volume < 0) chan->volume = 0;
                }
                break;
                
            case 0x6: // Vibrato + Volume slide
                // Vibrato processing would go here
                
                // Volume slide part
                if ((chan->effect_param & 0xF0) != 0) {
                    // Slide up
                    chan->volume += (chan->effect_param >> 4);
                    if (chan->volume > 64) chan->volume = 64;
                } else if ((chan->effect_param & 0x0F) != 0) {
                    // Slide down
                    chan->volume -= (chan->effect_param & 0x0F);
                    if (chan->volume < 0) chan->volume = 0;
                }
                break;
                
            case 0x7: // Tremolo
                // Tremolo processing would go here
                break;
                
            case 0xA: // Volume slide
                if ((chan->effect_param & 0xF0) != 0) {
                    // Slide up
                    chan->volume += (chan->effect_param >> 4);
                    if (chan->volume > 64) chan->volume = 64;
                } else if ((chan->effect_param & 0x0F) != 0) {
                    // Slide down
                    chan->volume -= (chan->effect_param & 0x0F);
                    if (chan->volume < 0) chan->volume = 0;
                }
                break;
                
            case 0xE: // Extended effects
                switch ((chan->effect_param >> 4) & 0x0F) {
                    case 0x9: // Retrigger note
                        if ((chan->effect_param & 0x0F) > 0) {
                            chan->retrigger_count++;
                            if (chan->retrigger_count >= (chan->effect_param & 0x0F)) {
                                chan->sample_pos = 0;
                                chan->retrigger_count = 0;
                            }
                        }
                        break;
                        
                    case 0xC: // Cut note
                        if (mod_player.current_tick == (chan->effect_param & 0x0F)) {
                            chan->volume = 0;
                        }
                        break;
                        
                    case 0xD: // Delay note
                        // Handled in row update
                        break;
                        
                    case 0xE: // Pattern delay
                        // Handled in row update
                        break;
                }
                break;
        }
        
        // Update channel parameters based on period
        if (chan->period > 0) {
            chan->sample_inc = (3579545 / chan->period) * 65536 / MOD_SAMPLE_RATE;
        }
    }
}

/**
 * Update for a new row
 */
void mod_update_row(void) {
    // Handle pattern delay
    if (mod_player.pattern_delay > 0) {
        mod_player.pattern_delay--;
        return;
    }
    
    // Move to next row
    mod_player.current_row++;
    if (mod_player.current_row >= MOD_ROWS_PER_PATTERN) {
        mod_player.current_row = 0;
        mod_player.current_pattern++;
        if (mod_player.current_pattern >= mod_player.header.song_length) {
            mod_player.current_pattern = 0; // Loop to beginning
        }
    }
    
    // Get pointer to current pattern data
    uint8_t pattern = mod_player.header.pattern_table[mod_player.current_pattern];
    uint8_t *row_data = mod_player.pattern_data + (pattern * MOD_ROWS_PER_PATTERN * 4 * 4) + (mod_player.current_row * 4 * 4);
    
    // Process data for each channel
    for (int i = 0; i < MOD_MAX_CHANNELS; i++) {
        // Extract note data from pattern
        uint32_t note_data = (row_data[0] << 24) | (row_data[1] << 16) | (row_data[2] << 8) | row_data[3];
        row_data += 4;
        
        uint16_t period = ((note_data >> 16) & 0x0FFF);
        uint8_t sample_num = ((note_data >> 24) & 0xF0) | ((note_data >> 12) & 0x0F);
        uint8_t effect = (note_data >> 8) & 0x0F;
        uint8_t param = note_data & 0xFF;
        
        MOD_Channel *chan = &mod_player.channels[i];
        
        // Save effect for tick processing
        chan->current_effect = effect;
        chan->effect_param = param;
        
        // Handle note and sample
        if (period > 0 && effect != 0x3 && effect != 0x5) {
            // Trigger note unless it's a portamento effect
            mod_trigger_note(i, period, sample_num);
        } else if (sample_num > 0) {
            // Just change sample without retriggering
            chan->sample_num = sample_num;
            chan->volume = mod_player.header.sample[sample_num - 1].volume;
        }
        
        // Handle row effects (those that happen on the first tick)
        mod_handle_effect(i, effect, param);
    }
}

/**
 * Update effects for all channels
 */
void mod_update_effects(void) {
    // This would process continuous effects like vibrato, tremolo, etc.
    // Implementation omitted for brevity
}

/**
 * Generate audio samples for output
 */
void mod_update_audio(int16_t *buffer, uint32_t len) {
    // Clear buffer
    memset(buffer, 0, len * 2 * sizeof(int16_t)); // Stereo output (2 samples)
    
    // If not playing, just return silent buffer
    if (!mod_player.playing) {
        return;
    }
    
    // Mix each channel's audio
    for (int c = 0; c < MOD_MAX_CHANNELS; c++) {
        MOD_Channel *chan = &mod_player.channels[c];
        
        // Skip if channel is silent
        if (chan->period == 0 || chan->volume == 0 || chan->sample_num == 0) {
            continue;
        }
        
        // Get sample data
        uint8_t *sample = mod_player.sample_data[chan->sample_num - 1];
        if (!sample) {
            continue;
        }
        
        uint16_t sample_len = mod_player.header.sample[chan->sample_num - 1].length * 2;
        uint16_t repeat_start = mod_player.header.sample[chan->sample_num - 1].repeat_offset * 2;
        uint16_t repeat_len = mod_player.header.sample[chan->sample_num - 1].repeat_length * 2;
        
        // Mix samples
        uint32_t pos = chan->sample_pos;
        for (uint32_t i = 0; i < len; i++) {
            // Check for end of sample
            if ((pos >> 16) >= sample_len) {
                if (repeat_len > 2) {
                    // Loop sample
                    pos = (repeat_start << 16) + (pos - (sample_len << 16));
                } else {
                    // Stop playing this sample
                    break;
                }
            }
            
            // Get sample value (8-bit signed)
            int8_t sample_val = (int8_t)sample[pos >> 16];
            
            // Apply volume
            int16_t val = (sample_val * chan->volume) / 64;
            
            // Mix into output buffer (simple stereo panning)
            int left_vol = 64 - (chan->panning >> 2);
            int right_vol = chan->panning >> 2;
            
            buffer[i*2] += (val * left_vol) / 64;
            buffer[i*2+1] += (val * right_vol) / 64;
            
            // Advance sample position
            pos += chan->sample_inc;
        }
        
        // Save sample position
        chan->sample_pos = pos;
    }
}

/**
 * Get period value for a note
 */
uint16_t mod_get_note_period(uint8_t note, uint8_t sample_num) {
    // This would look up the period from the period table
    // Implementation omitted for brevity
    return 0;
}

/**
 * Trigger a new note
 */
void mod_trigger_note(uint8_t channel, uint16_t period, uint8_t sample_num) {
    MOD_Channel *chan = &mod_player.channels[channel];
    
    chan->period = period;
    chan->sample_pos = 0;
    
    if (sample_num > 0) {
        chan->sample_num = sample_num;
        chan->volume = mod_player.header.sample[sample_num - 1].volume;
        
        // Set repeat points
        chan->repeat_start = mod_player.header.sample[sample_num - 1].repeat_offset * 2;
        chan->repeat_end = chan->repeat_start + mod_player.header.sample[sample_num - 1].repeat_length * 2;
    }
    
    // Update sample increment based on period
    if (period > 0) {
        chan->sample_inc = (3579545 / period) * 65536 / MOD_SAMPLE_RATE;
    }
}

/**
 * Handle pattern effects
 */
void mod_handle_effect(uint8_t channel, uint8_t effect, uint8_t param) {
    MOD_Channel *chan = &mod_player.channels[channel];
    
    switch (effect) {
        case 0x0: // Arpeggio
            chan->arpeggio[0] = 0;
            chan->arpeggio[1] = param >> 4;
            chan->arpeggio[2] = param & 0x0F;
            chan->arpeggio_pos = 0;
            break;
            
        case 0x3: // Tone portamento
            chan->target_period = chan->period;
            break;
            
        case 0x9: // Sample offset
            chan->sample_pos = (param << 8) * 256;
            break;
            
        case 0xB: // Position jump
            mod_player.current_pattern = param;
            mod_player.current_row = 0;
            break;
            
        case 0xC: // Set volume
            chan->volume = param > 64 ? 64 : param;
            break;
            
        case 0xD: // Pattern break
            mod_player.current_row = ((param >> 4) * 10) + (param & 0x0F);
            if (mod_player.current_row >= 64) mod_player.current_row = 0;
            mod_player.current_pattern++;
            break;
            
        case 0xE: // Extended effects
            switch ((param >> 4) & 0x0F) {
                case 0x0: // Set filter
                    // Filter on/off - not implemented
                    break;
                    
                case 0x1: // Fine portamento up
                    chan->period -= (param & 0x0F);
                    if (chan->period < 113) chan->period = 113;
                    break;
                    
                case 0x2: // Fine portamento down
                    chan->period += (param & 0x0F);
                    if (chan->period > 856) chan->period = 856;
                    break;
                    
                case 0x3: // Glissando control
                    chan->glissando = param & 0x0F;
                    break;
                    
                case 0x4: // Set vibrato waveform
                    // Vibrato waveform - not implemented
                    break;
                    
                case 0x5: // Set finetune
                    // Finetune - not implemented
                    break;
                    
                case 0x6: // Pattern loop
                    if ((param & 0x0F) == 0) {
                        // Set loop start
                        mod_player.pattern_loop_row = mod_player.current_row;
                    } else {
                        // Loop to start
                        if (mod_player.pattern_loop_count == 0) {
                            mod_player.pattern_loop_count = param & 0x0F;
                            mod_player.current_row = mod_player.pattern_loop_row - 1;
                        } else if (--mod_player.pattern_loop_count > 0) {
                            mod_player.current_row = mod_player.pattern_loop_row - 1;
                        }
                    }
                    break;
                    
                case 0x7: // Set tremolo waveform
                    // Tremolo waveform - not implemented
                    break;
                    
                case 0x8: // Fine panning (not in original ProTracker)
                    chan->panning = param & 0x0F;
                    break;
                    
                case 0xA: // Fine volume slide up
                    chan->volume += (param & 0x0F);
                    if (chan->volume > 64) chan->volume = 64;
                    break;
                    
                case 0xB: // Fine volume slide down
                    chan->volume -= (param & 0x0F);
                    if (chan->volume < 0) chan->volume = 0;
                    break;
                    
                case 0xE: // Pattern delay
                    mod_player.pattern_delay = param & 0x0F;
                    break;
            }
            break;
            
        case 0xF: // Set speed/tempo
            if (param <= 0x1F) {
                mod_player.ticks_per_row = param;
            } else {
                mod_player.bpm = param;
                mod_player.samples_per_tick = (MOD_SAMPLE_RATE * 60) / (mod_player.bpm * 4);
            }
            break;
    }
}
#include <dc/sound/sound.h>
#include <dc/sound/midi.h>

#include "arm/aica_cmd_iface.h"

void snd_midi(uint8 midi) {
	AICA_CMDSTR_MIDI(tmp, cmd, cmdmidi);

	cmd->cmd = AICA_CMD_MIDI;
	cmd->timestamp = 0;
	cmd->size = AICA_CMDSTR_MIDI_SIZE;
	*cmdmidi = midi;
	snd_sh4_to_aica(tmp, cmd->size);
}

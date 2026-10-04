#pragma once

/** Arka plan worker — tam entity IOCTL taramasi (overlay'de degil). */
void ExpectionalVoteRevealPollFromWorker();

/** Oy penceresi acik / canli oturum (UI + worker zamanlama). */
bool ExpectionalVoteRevealIsSessionActive() noexcept;

void ExpectionalDrawVoteKickRevealWindow();

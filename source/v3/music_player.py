# Speaking Timer-Clock v3 - music player engine
# MicroPython

import random


MODE_NORMAL = "normal"
MODE_SHUFFLE = "shuffle"
MODE_REPEAT = "repeat"
MODES = (MODE_NORMAL, MODE_SHUFFLE, MODE_REPEAT)


class MusicPlayer:
    """Folder-based music player with a no-repeat shuffle bag.

    Shuffle mode plays every track exactly once before a new cycle begins.
    The last `shuffle_guard` tracks from the previous cycle are placed at the
    end of the next cycle, preventing an immediate repeat across cycle edges.
    """

    def __init__(self, audio_queue, folder=8, track_count=45, shuffle_guard=7):
        self.audio = audio_queue
        self.folder = int(folder)
        self.track_count = max(1, int(track_count))
        self.shuffle_guard = max(0, min(int(shuffle_guard), self.track_count - 1))

        self.mode = MODE_SHUFFLE
        self.active = False
        self.paused = False
        self.current_track = 1
        self._waiting_for_end = False
        self._shuffle_bag = []
        self._recent = []
        self._play_history = []

    def set_mode(self, mode):
        if mode not in MODES:
            mode = MODE_SHUFFLE
        self.mode = mode
        if mode == MODE_SHUFFLE and not self._shuffle_bag:
            self._refill_shuffle_bag()
        return self.mode

    def cycle_mode(self):
        index = MODES.index(self.mode)
        return self.set_mode(MODES[(index + 1) % len(MODES)])

    def _shuffle(self, values):
        # Fisher-Yates works on MicroPython without depending on random.shuffle.
        for i in range(len(values) - 1, 0, -1):
            j = random.randint(0, i)
            values[i], values[j] = values[j], values[i]

    def _refill_shuffle_bag(self):
        all_tracks = list(range(1, self.track_count + 1))
        guarded = set(self._recent[-self.shuffle_guard:]) if self.shuffle_guard else set()

        safe = [track for track in all_tracks if track not in guarded]
        delayed = [track for track in all_tracks if track in guarded]
        self._shuffle(safe)
        self._shuffle(delayed)

        # The recently played tracks are deliberately at the end of the new
        # cycle. With 45 tracks and guard=7 this means at least 38 different
        # tracks appear before any of the previous cycle's final seven.
        self._shuffle_bag = safe + delayed

    def _remember(self, track):
        self._recent.append(track)
        if len(self._recent) > self.shuffle_guard:
            self._recent = self._recent[-self.shuffle_guard:]
        self._play_history.append(track)
        if len(self._play_history) > self.track_count * 2:
            self._play_history = self._play_history[-self.track_count:]

    def _next_track_number(self):
        if self.mode == MODE_REPEAT:
            return self.current_track

        if self.mode == MODE_NORMAL:
            return 1 if self.current_track >= self.track_count else self.current_track + 1

        if not self._shuffle_bag:
            self._refill_shuffle_bag()
        return self._shuffle_bag.pop(0)

    def _play(self, track, remember=True):
        track = max(1, min(self.track_count, int(track)))
        self.audio.clear(pause=True)
        self.audio.enqueue(self.folder, track)
        self.current_track = track
        self.active = True
        self.paused = False
        self._waiting_for_end = True
        if remember:
            self._remember(track)
        return track

    def start(self, track=None):
        if track is None:
            if self.mode == MODE_SHUFFLE:
                track = self._next_track_number()
            else:
                track = self.current_track
        return self._play(track)

    def stop(self):
        self.audio.clear(pause=True)
        self.active = False
        self.paused = False
        self._waiting_for_end = False

    def toggle_pause(self):
        if not self.active:
            self.start(self.current_track)
            return "playing"

        if self.paused:
            # Resume from the current track. AudioQueue pause currently clears
            # playback state, so replaying the current track is deterministic.
            self._play(self.current_track, remember=False)
            return "playing"

        self.audio.clear(pause=True)
        self.paused = True
        self._waiting_for_end = False
        return "paused"

    def next(self):
        track = self._next_track_number()
        return self._play(track)

    def previous(self):
        # Manual Previous is intentionally allowed to replay a track even in
        # shuffle mode. Automatic shuffle itself never repeats within a cycle.
        if len(self._play_history) >= 2:
            current = self._play_history.pop()
            previous = self._play_history.pop()
            self._play_history.append(current)
            return self._play(previous, remember=False)

        if self.mode == MODE_NORMAL:
            track = self.track_count if self.current_track <= 1 else self.current_track - 1
            return self._play(track)

        return self._play(self.current_track, remember=False)

    def service(self):
        if not self.active or self.paused or not self._waiting_for_end:
            return False
        if not self.audio.idle():
            return False

        self._waiting_for_end = False
        self.next()
        return True

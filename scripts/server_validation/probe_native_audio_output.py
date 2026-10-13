import argparse
from pathlib import Path
import json
import sys
import time
repository = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repository / '.cache/audio-probe-python'))
import pyaudiowpatch as audio_library
import numpy as numpy

parser = argparse.ArgumentParser(description='Measure playback loopback frequencies without microphone capture or retained sound recordings.')
parser.add_argument('label')
parser.add_argument('--output-directory', type=Path, default=repository / '.cache/audio-output-probe')
arguments = parser.parse_args()
label = arguments.label
if not label or any(character not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for character in label):
    parser.error('label must contain only letters, digits, hyphens or underscores')
audio_host = audio_library.PyAudio()
try:
    device = audio_host.get_default_wasapi_loopback()
    rate = int(device['defaultSampleRate'])
    channels = int(device['maxInputChannels'])
    started = time.time()
    stream = audio_host.open(format=audio_library.paFloat32, channels=channels, rate=rate, input=True, input_device_index=device['index'], frames_per_buffer=1024)
    try:
        blocks = [numpy.frombuffer(stream.read(1024, exception_on_overflow=False), dtype=numpy.float32).reshape(-1, channels).mean(axis=1) for block_index in range(int(rate * 3 / 1024))]
    finally:
        stream.stop_stream()
        stream.close()
    samples = numpy.concatenate(blocks)
    spectrum = numpy.abs(numpy.fft.rfft(samples * numpy.hanning(len(samples)))) / len(samples)
    frequencies = numpy.fft.rfftfreq(len(samples), 1 / rate)
    powers = {str(tone): float(spectrum[numpy.abs(frequencies - tone) < 3].max()) for tone in (523.25, 997.0)}
    report = {'label': label, 'started_epoch': started, 'ended_epoch': time.time(), 'rate': rate, 'channels': channels, 'sample_frames': len(samples), 'rms': float(numpy.sqrt(numpy.mean(samples * samples))), 'peak_frequency_hz': float(frequencies[int(numpy.argmax(spectrum))]), 'tone_amplitudes': powers, 'measurement': 'default Windows render endpoint loopback; frequency aggregates only; no microphone and no recording retained'}
    evidence = arguments.output_directory
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / f'output-{label}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
finally:
    audio_host.terminate()

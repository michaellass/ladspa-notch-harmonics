# ladspa-notch-harmonics

[![CI](https://github.com/michaellass/ladspa-notch-harmonics/workflows/CI/badge.svg)](https://github.com/michaellass/ladspa-notch-harmonics/actions/workflows/ci.yml)

This [LADSPA](https://www.ladspa.org/) plugin implements narrow notch filters at a specified base frequency and a specified number of harmonics.

Originally this plugin has been developed to block frequencies at 1 kHz and corresponding harmonics. This kind of noise is often observed in USB microphones due to cross talk between data and power lines in USB cables (a phenomenon called *USB whine*, *Frying Mosquitoes* or *Yeti Curse*) [[1]](https://0xstubs.org/understanding-usb-microphone-whine/). However, base frequency and the number of harmonics are configurable via the control ports of the plugin so that other scenarios can be handled (e.g., blocking 50/60 Hz noise or interference caused by external devices). By default, a maximum of 23 harmonics can be blocked. This limit can easily be changed in the source code to handle more demanding use cases.

[1] https://0xstubs.org/understanding-usb-microphone-whine/

## Compilation and Installation
To compile this plugin, you need the LADSPA headers installed in a location where your default compiler will look for it. Otherwise you may need to modify `Makefile` or set the `$CPATH` variable accordingly. In the best case, you can build this plugin by just running
```
make
```
and install it via
```
sudo make install
```

Note that by default `make install` will install the plugin into `/usr/local/lib/ladspa` which is not part of LADSPA's default search path. You can of course override the installation target via
```
sudo make DESTDIR=/usr/lib/ladspa install
```
set the LADSPA search path accordingly via
```
export LADSPA_PATH=/usr/local/lib/ladspa
```
or use absolute paths as in the PulseAudio example below.

## Real time filtering using PulseAudio
You can use PulseAudio's `module-ladspa-sink` module to implement real time filtering of your microphone output. This requires setting up some virtual sinks and sources. The following instructions are inspired by those at https://github.com/werman/noise-suppression-for-voice
```
pacmd load-module module-null-sink sink_name=mic_denoised_out sink_properties=device.description=Denoised_Microphone_AsSink
pacmd load-module module-ladspa-sink sink_name=mic_raw_in sink_master=mic_denoised_out label=notch_harmonics plugin=/usr/local/lib/ladspa/notch_harmonics_5761.so control=,
pacmd load-module module-loopback source=<your_mic_name> sink=mic_raw_in
```
Using the `control=<base freq>,<no harmonics>` argument, you can set base frequency and number of harmonics to remove from the audio stream. If one or both arguments are left out, the corresponding defaults are used (`control=1000,12`). `<your_mic_name>` can be determined by running `pacmd list-sources`.

Now set the input of your recording software to `Monitor of Denoised_Microphone_AsSink`. If the application does not support using monitor devices as input, run
```
pacmd load-module module-remap-source source_name=mic_denoised_in master=mic_denoised_out.monitor source_properties=device.description=Denoised_Microphone_AsSource
```
and then use `Denoised_Microphone_AsSource` as input device‌.

## Real time filtering using PipeWire
You can also use PipeWire to apply this filter in real time to your microphone input.

1. Create config directory in your user's home: `~/.config/pipewire/pipewire.conf.d/`
2. Create configuration file `~/.config/pipewire/pipewire.conf.d/99-input-notch-harmonics.conf` with the following contents:
    ```
    context.modules = [
    {   name = libpipewire-module-filter-chain
        args = {
            node.description =  "Notch Harmonics source"
            media.name =  "Notch Harmonics source"
            filter.graph = {
                nodes = [
                    {
                        type = ladspa
                        name = notch_harmonics
                        plugin = /usr/local/lib/ladspa/notch_harmonics_5761.so
                        label = notch_harmonics
                        control = {
                            "Base frequency" = 1000
                            "Number of harmonics" = 12
                        }
                    }
                ]
            }
            capture.props = {
                node.name =  "capture.notch_harmonics"
                node.passive = true
            }
            playback.props = {
                node.name =  "notch_harmonics"
                media.class = Audio/Source
            }
        }
    }
    ]
    ```
3. Restart PipeWire: `systemctl restart --user pipewire.service`
4. Select `Notch Harmonics source` as input device

## License

The code within this repository is licensed under the MIT license (see `LICENSE`). However, the LADSPA API and the corresponding header file (`ladspa.h`) are licensed under [LGPL 2.1](https://www.ladspa.org/lgpl.txt). If you include this plugin in any product, be aware of the impact of LADSPA's licensing or seek legal advice.

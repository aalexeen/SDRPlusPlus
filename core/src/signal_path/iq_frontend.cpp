#include "iq_frontend.h"
#include "../dsp/window/blackman.h"
#include "../dsp/window/nuttall.h"
#include <utils/flog.h>
#include <gui/gui.h>
#include <core.h>
#include <mutex>
#include <atomic>
#include <chrono>

IQFrontEnd::~IQFrontEnd() {
    if (!_init) { return; }

    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    try {
        stop();

        if (fftWindowBuf) {
            dsp::buffer::free(fftWindowBuf);
            fftWindowBuf = nullptr;
        }

        if (fftwPlan) {
            fftwf_destroy_plan(fftwPlan);
            fftwPlan = nullptr;
        }

        if (fftInBuf) {
            fftwf_free(fftInBuf);
            fftInBuf = nullptr;
        }

        if (fftOutBuf) {
            fftwf_free(fftOutBuf);
            fftOutBuf = nullptr;
        }

        flog::debug("IQFrontEnd destroyed safely");

    } catch (const std::exception& e) {
        flog::error("Exception in IQFrontEnd destructor: {}", e.what());
    }
}

void IQFrontEnd::init(dsp::stream<dsp::complex_t>* in, double sampleRate, bool buffering, int decimRatio, bool dcBlocking, int fftSize, double fftRate, FFTWindow fftWindow, float* (*acquireFFTBuffer)(void* ctx), void (*releaseFFTBuffer)(void* ctx), void* fftCtx) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    try {
        // Validate input parameters
        if (!in) {
            flog::error("IQFrontEnd::init - null input stream");
            return;
        }

        if (sampleRate <= 0 || fftSize <= 0 || fftRate <= 0) {
            flog::error("IQFrontEnd::init - invalid parameters: sampleRate={}, fftSize={}, fftRate={}",
                       sampleRate, fftSize, fftRate);
            return;
        }

        if (decimRatio < 1) {
            flog::error("IQFrontEnd::init - invalid decimation ratio: {}", decimRatio);
            return;
        }

        // Check if already initialized
        if (_init) {
            flog::warn("IQFrontEnd already initialized, reinitializing...");
            // Clean up existing resources
            if (fftWindowBuf) dsp::buffer::free(fftWindowBuf);
            if (fftwPlan) fftwf_destroy_plan(fftwPlan);
            if (fftInBuf) fftwf_free(fftInBuf);
            if (fftOutBuf) fftwf_free(fftOutBuf);
        }

        _sampleRate = sampleRate;
        _decimRatio = decimRatio;
        _fftSize = fftSize;
        _fftRate = fftRate;
        _fftWindow = fftWindow;
        _acquireFFTBuffer = acquireFFTBuffer;
        _releaseFFTBuffer = releaseFFTBuffer;
        _fftCtx = fftCtx;

        effectiveSr = _sampleRate / _decimRatio;

        inBuf.init(in);
        inBuf.bypass = !buffering;

        decim.init(NULL, _decimRatio);
        dcBlock.init(NULL, genDCBlockRate(effectiveSr));
        conjugate.init(NULL);

        preproc.init(&inBuf.out);
        preproc.addBlock(&decim, _decimRatio > 1);
        preproc.addBlock(&dcBlock, dcBlocking);
        preproc.addBlock(&conjugate, false); // TODO: Replace by parameter

        split.init(preproc.out);

        // TODO: Do something to avoid basically repeating this code twice
        int skip;
        genReshapeParams(effectiveSr, _fftSize, _fftRate, skip, _nzFFTSize);
        reshape.init(&fftIn, fftSize, skip);
        fftSink.init(&reshape.out, handler, this);

        // Allocate FFT window buffer
        fftWindowBuf = dsp::buffer::alloc<float>(_nzFFTSize);
        if (!fftWindowBuf) {
            flog::error("Failed to allocate FFT window buffer");
            return;
        }

        if (_fftWindow == FFTWindow::RECTANGULAR) {
            for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = 0; }
        }
        else if (_fftWindow == FFTWindow::BLACKMAN) {
            for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::blackman(i, _nzFFTSize); }
        }
        else if (_fftWindow == FFTWindow::NUTTALL) {
            for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::nuttall(i, _nzFFTSize); }
        }

        // Allocate FFTW buffers
        fftInBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
        fftOutBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));

        if (!fftInBuf || !fftOutBuf) {
            flog::error("Failed to allocate FFTW buffers");
            if (fftInBuf) { fftwf_free(fftInBuf); fftInBuf = nullptr; }
            if (fftOutBuf) { fftwf_free(fftOutBuf); fftOutBuf = nullptr; }
            if (fftWindowBuf) { dsp::buffer::free(fftWindowBuf); fftWindowBuf = nullptr; }
            return;
        }

        fftwPlan = fftwf_plan_dft_1d(_fftSize, fftInBuf, fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);
        if (!fftwPlan) {
            flog::error("Failed to create FFTW plan");
            fftwf_free(fftInBuf); fftInBuf = nullptr;
            fftwf_free(fftOutBuf); fftOutBuf = nullptr;
            dsp::buffer::free(fftWindowBuf); fftWindowBuf = nullptr;
            return;
        }

        // Clear the rest of the FFT input buffer
        dsp::buffer::clear(fftInBuf, _fftSize - _nzFFTSize, _nzFFTSize);

        split.bindStream(&fftIn);

        _init = true;
        initialized.store(true);
        healthy.store(true);

        flog::info("IQFrontEnd initialized successfully - SR: {}, FFT: {}, Decim: {}",
                  sampleRate, fftSize, decimRatio);

    } catch (const std::exception& e) {
        flog::error("Exception in IQFrontEnd::init: {}", e.what());
        healthy.store(false);
        _init = false;
        initialized.store(false);
    }
}

void IQFrontEnd::setInput(dsp::stream<dsp::complex_t>* in) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!in) {
        flog::error("IQFrontEnd::setInput - null input stream");
        healthy.store(false);
        return;
    }

    try {
        inBuf.setInput(in);
        flog::debug("IQFrontEnd input stream updated");
    } catch (const std::exception& e) {
        flog::error("Error setting IQ input: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::setSampleRate(double sampleRate) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!initialized.load()) {
        flog::error("Cannot set sample rate on uninitialized frontend");
        return;
    }

    if (sampleRate <= 0) {
        flog::error("Invalid sample rate: {}", sampleRate);
        return;
    }

    try {
        flog::debug("Setting sample rate to: {}", sampleRate);

        // Temp stop the necessary blocks
        dcBlock.tempStop();
        for (auto& [name, vfo] : vfos) {
            if (vfo) {
                vfo->tempStop();
            }
        }

        // Update the samplerate
        _sampleRate = sampleRate;
        effectiveSr = _sampleRate / _decimRatio;
        dcBlock.setRate(genDCBlockRate(effectiveSr));
        for (auto& [name, vfo] : vfos) {
            if (vfo) {
                vfo->setInSamplerate(effectiveSr);
            }
        }

        // Reconfigure the FFT
        updateFFTPath();

        // Restart blocks
        dcBlock.tempStart();
        for (auto& [name, vfo] : vfos) {
            if (vfo) {
                vfo->tempStart();
            }
        }

        flog::info("Sample rate updated to: {}", sampleRate);

    } catch (const std::exception& e) {
        flog::error("Sample rate change failed: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::setBuffering(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    try {
        inBuf.bypass = !enabled;
        flog::debug("Buffering {}", enabled ? "enabled" : "disabled");
    } catch (const std::exception& e) {
        flog::error("Error setting buffering: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::setDecimation(int ratio) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!initialized.load()) {
        flog::error("Cannot set decimation on uninitialized frontend");
        return;
    }

    if (ratio < 1) {
        flog::error("Invalid decimation ratio: {}", ratio);
        return;
    }

    try {
        flog::debug("Setting decimation ratio to: {}", ratio);

        // Temp stop the decimator
        decim.tempStop();

        // Update the decimation ratio
        _decimRatio = ratio;
        if (_decimRatio > 1) {
            decim.setRatio(_decimRatio);
        }
        setSampleRate(_sampleRate);

        // Restart the decimator if it was running
        decim.tempStart();

        // Enable or disable in the chain
        preproc.setBlockEnabled(&decim, _decimRatio > 1, [=](dsp::stream<dsp::complex_t>* out){
            split.setInput(out);
        });

        // Update the DSP sample rate (TODO: Find a way to get rid of this)
        core::setInputSampleRate(_sampleRate);

        flog::info("Decimation ratio updated to: {}", ratio);

    } catch (const std::exception& e) {
        flog::error("Decimation change failed: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::setDCBlocking(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    try {
        preproc.setBlockEnabled(&dcBlock, enabled, [=](dsp::stream<dsp::complex_t>* out){
            split.setInput(out);
        });
        flog::debug("DC blocking {}", enabled ? "enabled" : "disabled");
    } catch (const std::exception& e) {
        flog::error("Error setting DC blocking: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::setInvertIQ(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    try {
        preproc.setBlockEnabled(&conjugate, enabled, [=](dsp::stream<dsp::complex_t>* out){
            split.setInput(out);
        });
        flog::debug("IQ inversion {}", enabled ? "enabled" : "disabled");
    } catch (const std::exception& e) {
        flog::error("Error setting IQ inversion: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::bindIQStream(dsp::stream<dsp::complex_t>* stream) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!stream) {
        flog::error("Cannot bind null IQ stream");
        return;
    }

    try {
        split.bindStream(stream);
        flog::debug("IQ stream bound successfully");
    } catch (const std::exception& e) {
        flog::error("Error binding IQ stream: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::unbindIQStream(dsp::stream<dsp::complex_t>* stream) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!stream) {
        flog::error("Cannot unbind null IQ stream");
        return;
    }

    try {
        split.unbindStream(stream);
        flog::debug("IQ stream unbound successfully");
    } catch (const std::exception& e) {
        flog::error("Error unbinding IQ stream: {}", e.what());
        healthy.store(false);
    }
}

dsp::channel::RxVFO* IQFrontEnd::addVFO(std::string name, double sampleRate, double bandwidth, double offset) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!initialized.load()) {
        flog::error("Cannot add VFO to uninitialized frontend");
        return nullptr;
    }

    // Make sure no other VFO with that name already exists
    if (vfos.find(name) != vfos.end()) {
        flog::error("[IQFrontEnd] Tried to add VFO with existing name: {}", name);
        return nullptr;
    }

    // Validate parameters
    if (sampleRate <= 0 || bandwidth <= 0) {
        flog::error("[IQFrontEnd] Invalid VFO parameters: sampleRate={}, bandwidth={}",
                   sampleRate, bandwidth);
        return nullptr;
    }

    // Warn about too many VFOs
    if (vfos.size() >= 8) {
        flog::warn("[IQFrontEnd] High number of VFOs ({}). Performance may be affected.", vfos.size());
    }

    try {
        // Create VFO and its input stream
        dsp::stream<dsp::complex_t>* vfoIn = new dsp::stream<dsp::complex_t>;
        if (!vfoIn) {
            flog::error("[IQFrontEnd] Failed to allocate VFO input stream");
            return nullptr;
        }

        dsp::channel::RxVFO* vfo = new dsp::channel::RxVFO(vfoIn, effectiveSr, sampleRate, bandwidth, offset);
        if (!vfo) {
            flog::error("[IQFrontEnd] Failed to allocate VFO");
            delete vfoIn;
            return nullptr;
        }

        // Register them
        vfoStreams[name] = vfoIn;
        vfos[name] = vfo;
        bindIQStream(vfoIn);

        // Start VFO
        vfo->start();

        flog::info("VFO '{}' added successfully (SR: {}, BW: {}, Offset: {})",
                  name, sampleRate, bandwidth, offset);
        return vfo;

    } catch (const std::exception& e) {
        flog::error("Exception adding VFO '{}': {}", name, e.what());
        healthy.store(false);
        return nullptr;
    }
}

void IQFrontEnd::removeVFO(std::string name) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    // Make sure that a VFO with that name exists
    if (vfos.find(name) == vfos.end()) {
        flog::error("[IQFrontEnd] Tried to remove a VFO that doesn't exist: {}", name);
        return;
    }

    try {
        // Remove the VFO and stream from registry
        dsp::stream<dsp::complex_t>* vfoIn = vfoStreams[name];
        dsp::channel::RxVFO* vfo = vfos[name];

        // Stop the VFO safely
        if (vfo) {
            vfo->stop();
        }

        if (vfoIn) {
            unbindIQStream(vfoIn);
        }

        vfoStreams.erase(name);
        vfos.erase(name);

        // Delete the VFO and its input stream
        delete vfo;
        delete vfoIn;

        flog::info("VFO '{}' removed successfully", name);

    } catch (const std::exception& e) {
        flog::error("Exception removing VFO '{}': {}", name, e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::setFFTSize(int size) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!initialized.load()) {
        flog::error("Cannot set FFT size on uninitialized frontend");
        return;
    }

    if (size <= 0 || (size & (size - 1)) != 0) {  // Check if power of 2
        flog::error("Invalid FFT size: {} (must be power of 2)", size);
        return;
    }

    try {
        flog::debug("Setting FFT size to: {}", size);
        _fftSize = size;
        updateFFTPath(true);
        flog::info("FFT size updated to: {}", size);
    } catch (const std::exception& e) {
        flog::error("FFT size change failed: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::setFFTRate(double rate) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!initialized.load()) {
        flog::error("Cannot set FFT rate on uninitialized frontend");
        return;
    }

    if (rate <= 0) {
        flog::error("Invalid FFT rate: {}", rate);
        return;
    }

    try {
        flog::debug("Setting FFT rate to: {}", rate);
        _fftRate = rate;
        updateFFTPath();
        flog::info("FFT rate updated to: {}", rate);
    } catch (const std::exception& e) {
        flog::error("FFT rate change failed: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::setFFTWindow(FFTWindow fftWindow) {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!initialized.load()) {
        flog::error("Cannot set FFT window on uninitialized frontend");
        return;
    }

    try {
        flog::debug("Setting FFT window type");
        _fftWindow = fftWindow;
        updateFFTPath();
        flog::info("FFT window updated");
    } catch (const std::exception& e) {
        flog::error("FFT window change failed: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::flushInputBuffer() {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    try {
        inBuf.flush();
        flog::debug("Input buffer flushed");
    } catch (const std::exception& e) {
        flog::error("Error flushing input buffer: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::start() {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    if (!initialized.load()) {
        flog::error("Cannot start uninitialized frontend");
        return;
    }

    try {
        flog::debug("Starting IQFrontEnd components");

        // Start input buffer
        inBuf.start();

        // Start pre-proc chain (automatically start all bound blocks)
        preproc.start();

        // Start IQ splitter
        split.start();

        // Start all VFOs
        for (auto& [name, vfo] : vfos) {
            if (vfo) {
                vfo->start();
            }
        }

        // Start FFT chain
        reshape.start();
        fftSink.start();

        healthy.store(true);
        flog::info("IQFrontEnd started successfully");

    } catch (const std::exception& e) {
        flog::error("Error starting IQFrontEnd: {}", e.what());
        healthy.store(false);
    }
}

void IQFrontEnd::stop() {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);

    try {
        flog::debug("Stopping IQFrontEnd components");

        // Stop input buffer
        inBuf.stop();

        // Stop pre-proc chain (automatically stop all bound blocks)
        preproc.stop();

        // Stop IQ splitter
        split.stop();

        // Stop all VFOs
        for (auto& [name, vfo] : vfos) {
            if (vfo) {
                vfo->stop();
            }
        }

        // Stop FFT chain
        reshape.stop();
        fftSink.stop();

        flog::info("IQFrontEnd stopped successfully");

    } catch (const std::exception& e) {
        flog::error("Error stopping IQFrontEnd: {}", e.what());
        healthy.store(false);
    }
}

double IQFrontEnd::getEffectiveSamplerate() {
    std::lock_guard<std::recursive_mutex> lock(frontendMutex);
    return effectiveSr;
}

bool IQFrontEnd::isHealthy() const {
    return healthy.load() && initialized.load();
}

bool IQFrontEnd::isInitialized() const {
    return initialized.load();
}

void IQFrontEnd::handler(dsp::complex_t* data, int count, void* ctx) {
    IQFrontEnd* _this = (IQFrontEnd*)ctx;

    if (!_this || !data || count <= 0) {
        flog::error("Invalid parameters in FFT handler");
        return;
    }

    // Quick health check without locking
    if (!_this->healthy.load() || !_this->initialized.load()) {
        return;
    }

    try {
        // Validate buffer bounds
        if (count > _this->_nzFFTSize) {
            flog::warn("FFT handler: data count ({}) exceeds buffer size ({})", count, _this->_nzFFTSize);
            count = _this->_nzFFTSize;
        }

        // Check buffers are allocated
        if (!_this->fftInBuf || !_this->fftOutBuf || !_this->fftWindowBuf) {
            flog::error("FFT buffers not allocated");
            _this->healthy.store(false);
            return;
        }

        // Apply window
        volk_32fc_32f_multiply_32fc((lv_32fc_t*)_this->fftInBuf, (lv_32fc_t*)data,
                                   _this->fftWindowBuf, count);

        // Execute FFT
        fftwf_execute(_this->fftwPlan);

        // Acquire buffer
        float* fftBuf = nullptr;
        if (_this->_acquireFFTBuffer) {
            fftBuf = _this->_acquireFFTBuffer(_this->_fftCtx);
        }

        // Convert the complex output of the FFT to dB amplitude
        if (fftBuf) {
            volk_32fc_s32f_power_spectrum_32f(fftBuf, (lv_32fc_t*)_this->fftOutBuf,
                                             _this->_fftSize, _this->_fftSize);
        }

        // Release buffer
        if (_this->_releaseFFTBuffer) {
            _this->_releaseFFTBuffer(_this->_fftCtx);
        }

    } catch (const std::exception& e) {
        flog::error("Exception in FFT handler: {}", e.what());
        _this->healthy.store(false);
    }
}

void IQFrontEnd::updateFFTPath(bool updateWaterfall) {
    // Note: This function is called from within locked contexts, so no additional locking needed

    if (!initialized.load()) {
        flog::error("Cannot update FFT path on uninitialized frontend");
        return;
    }

    try {
        flog::debug("Updating FFT path");

        // Temp stop branch
        reshape.tempStop();
        fftSink.tempStop();

        // Update reshaper settings
        int skip;
        genReshapeParams(effectiveSr, _fftSize, _fftRate, skip, _nzFFTSize);
        reshape.setKeep(_nzFFTSize);
        reshape.setSkip(skip);

        // Update window
        if (fftWindowBuf) {
            dsp::buffer::free(fftWindowBuf);
        }
        fftWindowBuf = dsp::buffer::alloc<float>(_nzFFTSize);
        if (!fftWindowBuf) {
            flog::error("Failed to allocate FFT window buffer during update");
            healthy.store(false);
            return;
        }

        if (_fftWindow == FFTWindow::RECTANGULAR) {
            for (int i = 0; i < _nzFFTSize; i++) {
                fftWindowBuf[i] = 1.0f * ((i % 2) ? -1.0f : 1.0f);
            }
        }
        else if (_fftWindow == FFTWindow::BLACKMAN) {
            for (int i = 0; i < _nzFFTSize; i++) {
                fftWindowBuf[i] = dsp::window::blackman(i, _nzFFTSize) * ((i % 2) ? -1.0f : 1.0f);
            }
        }
        else if (_fftWindow == FFTWindow::NUTTALL) {
            for (int i = 0; i < _nzFFTSize; i++) {
                fftWindowBuf[i] = dsp::window::nuttall(i, _nzFFTSize) * ((i % 2) ? -1.0f : 1.0f);
            }
        }

        // Update FFT plan
        if (fftInBuf) fftwf_free(fftInBuf);
        if (fftOutBuf) fftwf_free(fftOutBuf);
        if (fftwPlan) fftwf_destroy_plan(fftwPlan);

        fftInBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
        fftOutBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));

        if (!fftInBuf || !fftOutBuf) {
            flog::error("Failed to allocate FFTW buffers during update");
            healthy.store(false);
            return;
        }

        fftwPlan = fftwf_plan_dft_1d(_fftSize, fftInBuf, fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);
        if (!fftwPlan) {
            flog::error("Failed to create FFTW plan during update");
            healthy.store(false);
            return;
        }

        // Clear the rest of the FFT input buffer
        dsp::buffer::clear(fftInBuf, _fftSize - _nzFFTSize, _nzFFTSize);

        // Update waterfall (TODO: This is annoying, it makes this module non testable and will constantly clear the waterfall for any reason)
        if (updateWaterfall) {
            gui::waterfall.setRawFFTSize(_fftSize);
        }

        // Restart branch
        reshape.tempStart();
        fftSink.tempStart();

        flog::debug("FFT path updated successfully");

    } catch (const std::exception& e) {
        flog::error("Error updating FFT path: {}", e.what());
        healthy.store(false);
    }
}
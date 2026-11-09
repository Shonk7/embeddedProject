%% mixer_design.m
% ELEC4123 Embedded elective - Extension A: 3-band audio mixer
%
% What this script does
%   1. Designs the crossover filters for the 3-band mixer.
%   2. Runs them in the SAME integer (fixed-point) arithmetic the STM32
%      will use, so any rounding problem shows up here, not on the board.
%   3. Tests the mixer with (a) an impulse and (b) a composite multitone
%      waveform, and compares the result with the design and the UI request.
%   4. Prints the integer coefficients to paste into the C code.
%
% Filter structure (Linkwitz-Riley, 4th order = "LR4")
%   low  = LP(fc1) -> LP(fc1)
%   mid  = HP(fc1) -> HP(fc1) -> LP(fc2) -> LP(fc2)
%   high = HP(fc2) -> HP(fc2)
%   y    = g1*low + g2*mid + g3*high          (g = slider gains, 0..2)
% Each LP/HP block is one 2nd-order Butterworth biquad (Q = 1/sqrt(2)).
% Two identical Butterworth biquads in series make one LR4 filter.

clear; close all; clc;

%% 1. Parameters (change testGains to test filters)
fx  = 399.25e3;          % team sampling clock (Hz)
fs  = fx/8;              % the mixer runs at fx/8 = 49.906 kHz
fc1 = 500;               % low/mid crossover (Hz), soft goal range 250..750
fc2 = 4000;              % mid/high crossover (Hz), soft goal range 2000..6000
testGains = [2 0.5 1.3];   % [low mid high]

% Fixed-point formats - these MUST match the C code
P.COEF_BITS  = 30;       % coefficients stored as int32 in Q2.30 (range +-2)
P.STATE_BITS = 8;        % samples are shifted left 8 bits inside the filters
P.GAIN_BITS  = 14;       % slider gain stored as integer: 1.0 -> 16384, 2.0 -> 32768

%% 2. Design the biquads
% Formulas: R. Bristow-Johnson, "Audio EQ Cookbook" (bilinear transform).
% Simple enough to also run on the STM32 when the cutoffs are changed.
Q   = 1/sqrt(2);                         % Butterworth
%Q = 0.67;                                % Random bs testing
lp1 = rbjLowpass (fc1, fs, Q);
hp1 = rbjHighpass(fc1, fs, Q);
lp2 = rbjLowpass (fc2, fs, Q);
hp2 = rbjHighpass(fc2, fs, Q);

% Each band = a list of biquads applied one after the other
bands     = { {lp1, lp1}, {hp1, hp1, lp2, lp2}, {hp2, hp2} };
bandNames = {'Low', 'Mid', 'High'};

%% 3. Designed (ideal, double-precision) frequency response
f = logspace(log10(20), log10(fs/2), 2000);    % frequency axis (Hz)
Hband = zeros(3, numel(f));
for k = 1:3
    Hband(k,:) = chainResponse(bands{k}, f, fs);
end

% Tone colour: with all gains equal the bands must add back to a flat line
Hsum = sum(Hband, 1);
fprintf('Equal gains, designed: |low+mid+high| = %.3f to %.3f dB\n', ...
        min(dB(Hsum)), max(dB(Hsum)));

% Why not the 2-filter shortcut mid = x - low - high? (see Figure 2)
midSubBW2 = 1 - chainResponse({lp1}, f, fs) - chainResponse({hp2}, f, fs);
midSubLR4 = 1 - Hband(1,:) - Hband(3,:);
fprintf('Shortcut mid band peak: %+.2f dB (Butterworth-2), %+.2f dB (LR4); LR4 band-pass peak %+.2f dB\n', ...
        max(dB(midSubBW2)), max(dB(midSubLR4)), max(dB(Hband(2,:))));

% Stability margin: the closer a pole is to 1, the more rounding matters
fprintf('Largest pole radius (LP at fc1): %.4f\n\n', max(abs(roots(lp1.a))));

%% 4. Test A - impulse through the fixed-point mixer
% The FFT of the impulse response IS the frequency response, at every
% frequency at once. Amplitude 1000 ADC counts keeps 2x gain below 2047.
Nimp = 8192;
imp  = zeros(1, Nimp);  imp(1) = 1000;
fImp = (1:Nimp/2-1) * fs/Nimp;                 % FFT bin frequencies (skip DC)

HimpBand = zeros(3, numel(fImp));
for k = 1:3
    g = zeros(1,3);  g(k) = 1;                 % one band on, others off
    [~, yFine] = mixerFixed(imp, bands, g, P);
    HimpBand(k,:) = impulseToResponse(yFine, 1000);
end
[~, yFine] = mixerFixed(imp, bands, [1 1 1], P);    HimpSum  = impulseToResponse(yFine, 1000);
[~, yFine] = mixerFixed(imp, bands, testGains, P);  HimpTest = impulseToResponse(yFine, 1000);

inBand = fImp <= 5000;                         % max input frequency is 5 kHz
fprintf('Equal gains, fixed point: %.3f to %.3f dB (20 Hz - 5 kHz)\n', ...
        min(dB(HimpSum(inBand & fImp >= 20))), max(dB(HimpSum(inBand & fImp >= 20))));

% Fixed point vs design at the test gains
Htest  = testGains * Hband;                    % designed complex response
HtestI = interp1(f, abs(Htest), fImp(inBand & fImp >= 20));
errdB  = dB(HimpTest(inBand & fImp >= 20)) - dB(HtestI);
fprintf('Fixed point vs design at gains [%g %g %g]: max error %.3f dB\n\n', ...
        testGains, max(abs(errdB)));

%% 5. Test B - composite multitone waveform (mirrors the bench test)
% Tones inside each band and at both crossovers. Each tone is snapped to
% an exact FFT bin so it has a whole number of cycles in the analysis
% window -> no spectral leakage, every bin gives a clean gain.
Nfft     = 8192;
fWanted  = [100 250 fc1 1000 2000 fc2 4500];
kBin     = round(fWanted * Nfft / fs);         % FFT bin of each tone
fTone    = kBin * fs / Nfft;                   % actual tone frequencies
amp      = 120;                                % per-tone amplitude (ADC counts)

n = 0 : 2*Nfft-1;
x = round(amp * sum(sin(2*pi*fTone(:)*n/fs), 1));   % integer ADC samples
y = mixerFixed(x, bands, testGains, P);

% Ignore the first block (filters starting up), measure on the second
X = fft(x(Nfft+1:end));
Y = fft(y(Nfft+1:end));
gMeasured = abs(Y(kBin+1)) ./ abs(X(kBin+1));
gDesigned = abs(testGains * [chainResponse(bands{1}, fTone, fs);
                             chainResponse(bands{2}, fTone, fs);
                             chainResponse(bands{3}, fTone, fs)]);

fprintf('Multitone test, gains [%g %g %g]\n', testGains);
fprintf('  %8s  %-10s  %9s  %9s  %8s\n', 'f (Hz)', 'UI request', 'designed', 'measured', 'error');
for i = 1:numel(fTone)
    fprintf('  %8.1f  %-10s  %9.3f  %9.3f  %+7.3f dB\n', fTone(i), ...
            uiRequest(fWanted(i), fc1, fc2, testGains), gDesigned(i), gMeasured(i), ...
            dB(gMeasured(i)) - dB(gDesigned(i)));
end
fprintf('  Output peak %d counts (clips at 2047)\n\n', max(abs(y)));

%% 6. Coefficients for the C code (int32, Q2.30)
% The C filter computes acc = b0*x0 + b1*x1 + b2*x2 - a1*y1 - a2*y2 + err
fprintf('C coefficients, Q2.30, order {b0, b1, b2, a1, a2}:\n');
printCoeffs('lp1', lp1, P);  printCoeffs('hp1', hp1, P);
printCoeffs('lp2', lp2, P);  printCoeffs('hp2', hp2, P);

%% 7. Computational cost (count only - measure real cycles with DWT on the board)
nBiquads = sum(cellfun(@numel, bands));
fprintf('\nCost per sample: %d biquads x 5 MACs + 3 gain MACs = %d multiply-accumulates\n', ...
        nBiquads, 5*nBiquads + 3);
fprintf('Budget at fx/8: 72 MHz / %.0f Hz = %.0f CPU cycles per sample\n', fs, 72e6/fs);

%% 8. Figures (3 only)
% Figure 1 - the three bands and their sum (tone colour check)
figure('Name', 'Band responses');
colours = lines(3);
for k = 1:3
    semilogx(f, dB(Hband(k,:)), 'Color', colours(k,:), 'LineWidth', 1.5); hold on;
end
semilogx(f, dB(Hsum), 'k', 'LineWidth', 1.5);
for k = 1:3
    semilogx(fImp, dB(HimpBand(k,:)), ':', 'Color', colours(k,:), 'LineWidth', 1.5);
end
plot([fc1 fc1], [-60 10], 'k--');  plot([fc2 fc2], [-60 10], 'k--');
grid on; axis([20 fs/2 -60 6]);
xlabel('Frequency (Hz)'); ylabel('Gain (dB)');
title(sprintf('LR4 bands, fc = %g Hz / %g Hz (solid: design, dotted: fixed-point impulse)', fc1, fc2));
legend([bandNames, {'Sum (all gains = 1)'}], 'Location', 'southwest');

% Figure 2 - why the 2-filter shortcut (mid = x - low - high) was rejected
figure('Name', 'Mid band: shortcut vs band-pass');
semilogx(f, dB(midSubBW2), 'LineWidth', 1.5); hold on;
semilogx(f, dB(midSubLR4), 'LineWidth', 1.5);
semilogx(f, dB(Hband(2,:)), 'k', 'LineWidth', 1.5);
plot([fc1 fc1], [-30 10], 'k--');  plot([fc2 fc2], [-30 10], 'k--');
grid on; axis([20 fs/2 -30 6]);
xlabel('Frequency (Hz)'); ylabel('Mid band gain (dB)');
title('Mid band alone (mid slider = 1, others = 0)');
legend('x - low - high (Butterworth-2)', 'x - low - high (LR4)', 'LR4 band-pass (used)', ...
       'Location', 'south');

% Figure 3 - mixer at the test gains: UI request vs design vs both tests
figure('Name', 'Mixer at test gains');
semilogx(f, dB(Htest), 'k', 'LineWidth', 1.5); hold on;
semilogx(fImp, dB(HimpTest), ':', 'Color', [0.85 0.33 0.1], 'LineWidth', 1.5);
semilogx(fTone, dB(gMeasured), 'o', 'MarkerSize', 8, 'LineWidth', 1.5);
uiLine = testGains(1)*(f < fc1) + testGains(2)*(f >= fc1 & f < fc2) + testGains(3)*(f >= fc2);
semilogx(f, dB(uiLine), '--', 'Color', [0.5 0.5 0.5]);
grid on; axis([20 fs/2 -12 9]);
xlabel('Frequency (Hz)'); ylabel('Gain (dB)');
title(sprintf('Gains [%g %g %g]', testGains));
legend('Design', 'Fixed-point impulse', 'Fixed-point multitone', 'UI request (ideal bands)', ...
       'Location', 'southwest');


%% ===================== Local functions =====================

function bq = rbjLowpass(fc, fs, Q)
% 2nd-order low-pass biquad (Audio EQ Cookbook), normalised so a(1) = 1
w     = 2*pi*fc/fs;
alpha = sin(w) / (2*Q);
b = [(1-cos(w))/2,  1-cos(w),  (1-cos(w))/2];
a = [1+alpha,      -2*cos(w),   1-alpha];
bq.b = b / a(1);
bq.a = a / a(1);
end

function bq = rbjHighpass(fc, fs, Q)
% 2nd-order high-pass biquad (Audio EQ Cookbook), normalised so a(1) = 1
w     = 2*pi*fc/fs;
alpha = sin(w) / (2*Q);
b = [(1+cos(w))/2, -(1+cos(w)), (1+cos(w))/2];
a = [1+alpha,      -2*cos(w),    1-alpha];
bq.b = b / a(1);
bq.a = a / a(1);
end

function H = chainResponse(chain, f, fs)
% Complex frequency response of biquads in series = product of each one
zinv = exp(-1j*2*pi*f/fs);                     % z^-1 on the unit circle
H = ones(size(f));
for s = 1:numel(chain)
    b = chain{s}.b;  a = chain{s}.a;
    H = H .* (b(1) + b(2)*zinv + b(3)*zinv.^2) ./ (a(1) + a(2)*zinv + a(3)*zinv.^2);
end
end

function y = biquadFixed(bq, x, COEF_BITS)
% Direct Form I biquad in integer arithmetic, exactly as the C code does it:
%   acc  = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2] + err   (int64)
%   y[n] = acc >> 30                 (drop the 30 fraction bits)
%   err  = acc - (y[n] << 30)        (the part that was dropped)
% "err" is carried into the next sample (first-order error feedback). Without
% it, plain rounding can leave the 500 Hz low-pass stuck at a small constant
% output after the input goes quiet (a "deadband" limit cycle) and gives a
% DC gain error. With it, nothing is lost: the DC gain is exact.
% MATLAB doubles hold these integers exactly (all values stay below 2^53).
b   = round(bq.b * 2^COEF_BITS);
a   = round(bq.a * 2^COEF_BITS);
one = 2^COEF_BITS;
y = zeros(size(x));
x1 = 0; x2 = 0; y1 = 0; y2 = 0; err = 0;      % filter memory (states)
for n = 1:numel(x)
    acc  = b(1)*x(n) + b(2)*x1 + b(3)*x2 - a(2)*y1 - a(3)*y2 + err;
    y(n) = floor(acc / one);                  % arithmetic ">> 30" = floor
    err  = acc - y(n)*one;                    % remainder, 0 .. 2^30-1
    x2 = x1;  x1 = x(n);
    y2 = y1;  y1 = y(n);
end
end

function [y, yFine] = mixerFixed(x, bands, gains, P)
% Bit-exact model of the whole C mixer for one block of samples.
% x: integer input samples with the 2048 ADC offset already removed.
% y:     what the DAC receives (rounded to whole counts, saturated).
% yFine: same signal before that final rounding - used by the impulse
%        test, whose tiny tail values would otherwise be lost to rounding.
xs   = x * 2^P.STATE_BITS;                     % C: x << 8 (extra fraction bits)
gInt = round(gains * 2^P.GAIN_BITS);           % slider value -> integer gain
acc  = zeros(size(x));
for k = 1:numel(bands)
    v = xs;
    for s = 1:numel(bands{k})                  % run the band's biquads in series
        v = biquadFixed(bands{k}{s}, v, P.COEF_BITS);
    end
    acc = acc + gInt(k) * v;                   % C: int64 sum of gain * band
end
shift = P.GAIN_BITS + P.STATE_BITS;
yFine = acc / 2^shift;
y = floor((acc + 2^(shift-1)) / 2^shift);      % round back to ADC counts
y = min(max(y, -2048), 2047);                  % saturate to the 12-bit range
end

function H = impulseToResponse(y, amplitude)
% FFT of an impulse response -> frequency response at bins 1 .. N/2-1
Y = fft(y) / amplitude;
H = Y(2 : numel(y)/2);
end

function s = uiRequest(f, fc1, fc2, g)
% Gain the UI asks for at frequency f (text, for the printed table)
if f == fc1 || f == fc2
    s = 'crossover';
elseif f < fc1
    s = sprintf('%g', g(1));
elseif f < fc2
    s = sprintf('%g', g(2));
else
    s = sprintf('%g', g(3));
end
end

function printCoeffs(name, bq, P)
c = round([bq.b, bq.a(2:3)] * 2^P.COEF_BITS);
fprintf('  %s = {%11d, %11d, %11d, %11d, %11d};\n', name, c);
end

function d = dB(h)
d = 20*log10(abs(h) + 1e-12);
end
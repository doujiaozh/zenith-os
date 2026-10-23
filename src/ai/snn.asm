; =============================================================================
;  ZenithOS - ai/snn.asm
;  A deterministic 24-neuron spiking neural network that lives in kernel space
;  and drives the desktop.  This is the "AI-native" core: the UI is a
;  perception loop around an actual inference loop, not a static animation.
;
;  Model
;    - 24 leaky integrate-and-fire neurons, one byte of activation each
;    - every systick: a = a - (a >> 3)               (12.5% leak)
;    - random Poisson-ish stimulus: 1/8 ticks inject +72 into a pseudo-random
;      neuron (xorshift32, fixed seed -> reproducible frames)
;    - neurons above SNN_THRESHOLD spike: activation resets to 0, spikes
;      propagate once through a sparse 24x24 weight matrix (weights 1..40)
;    - keyboard WASD events inject extra stimulus (snn_stimulate)
;
;  Everything is saturating: activations stay in 0..255, so no fault can be
;  caused by a runaway network.  The UI renders activations as glow and
;  travelling packets.
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

; -----------------------------------------------------------------------------
; snn_init: zero the state and synthesise the weight matrix from a fixed seed
; -----------------------------------------------------------------------------
snn_init:
    pushad
    mov edi, snn_activation
    mov ecx, SNN_NEURONS
    xor eax, eax
    rep stosb
    mov edi, snn_spiked
    mov ecx, SNN_NEURONS
    rep stosb
    mov dword [snn_spike_count], 0
    mov dword [snn_spikes_this_tick], 0
    mov dword [snn_energy], 0
    mov dword [snn_random], 0x13579BDF

    ; Sparse excitatory weights: xorshift32 decides connectivity.
    mov edi, snn_weights
    mov ecx, SNN_NEURONS * SNN_NEURONS
.gen:
    mov eax, [snn_random]
    mov edx, eax
    shl edx, 13
    xor eax, edx
    mov edx, eax
    shr edx, 17
    xor eax, edx
    mov edx, eax
    shl edx, 5
    xor eax, edx
    mov [snn_random], eax

    mov edx, eax
    and edx, 0x3F
    mov esi, eax
    and esi, 0xC0
    jz .no_weight
    cmp edx, 40
    jbe .store
    mov edx, 40
.store:
    mov [edi], dl
    inc edi
    dec ecx
    jnz .gen
    popad
    ret
.no_weight:
    mov byte [edi], 0
    inc edi
    dec ecx
    jnz .gen
    popad
    ret

; -----------------------------------------------------------------------------
; snn_tick: advance the network by one systick (100 Hz)
; -----------------------------------------------------------------------------
snn_tick:
    pushad
    mov dword [snn_spikes_this_tick], 0
    mov edi, snn_spiked
    mov ecx, SNN_NEURONS
    xor eax, eax
    rep stosb

    ; 1. leaky decay
    mov esi, snn_activation
    mov ecx, SNN_NEURONS
.decay:
    movzx eax, byte [esi]
    mov edx, eax
    shr edx, 3
    sub eax, edx
    mov [esi], al
    inc esi
    dec ecx
    jnz .decay

    ; 2. stochastic stimulus (1 in 8 ticks)
    mov eax, [snn_random]
    mov edx, eax
    shl edx, 13
    xor eax, edx
    mov edx, eax
    shr edx, 17
    xor eax, edx
    mov edx, eax
    shl edx, 5
    xor eax, edx
    mov [snn_random], eax
    mov ebx, eax
    and ebx, 3
    jnz .no_stimulus
    mov ecx, SNN_NEURONS
    xor edx, edx
    div ecx
    movzx eax, byte [snn_activation + edx]
    add eax, 88
    cmp eax, 255
    jbe .stim_store
    mov eax, 255
.stim_store:
    mov [snn_activation + edx], al
.no_stimulus:

    ; 3. integrate + fire + propagate
    xor edi, edi                      ; neuron index
.neurons:
    movzx eax, byte [snn_activation + edi]
    cmp eax, SNN_THRESHOLD
    jb .next_neuron
    mov byte [snn_activation + edi], 0
    mov byte [snn_spiked + edi], 1
    inc dword [snn_spike_count]
    inc dword [snn_spikes_this_tick]

    imul esi, edi, SNN_NEURONS
    lea esi, [snn_weights + esi]
    xor ecx, ecx
.neighbours:
    movzx edx, byte [esi + ecx]
    test edx, edx
    jz .next_neighbour
    movzx eax, byte [snn_activation + ecx]
    add eax, edx
    cmp eax, 255
    jbe .prop_store
    mov eax, 255
.prop_store:
    mov [snn_activation + ecx], al
.next_neighbour:
    inc ecx
    cmp ecx, SNN_NEURONS
    jb .neighbours
.next_neuron:
    inc edi
    cmp edi, SNN_NEURONS
    jb .neurons

    ; 4. energy = sum(activations)
    xor eax, eax
    mov esi, snn_activation
    mov ecx, SNN_NEURONS
.sum:
    movzx edx, byte [esi]
    add eax, edx
    inc esi
    dec ecx
    jnz .sum
    mov [snn_energy], eax
    popad
    ret

; -----------------------------------------------------------------------------
; snn_stimulate: EAX = neuron index (0..23), injects +96 (saturating)
; Called by the UI when WASD/Space is pressed so input really enters the net.
; -----------------------------------------------------------------------------
snn_stimulate:
    cmp eax, SNN_NEURONS
    jae .done
    movzx edx, byte [snn_activation + eax]
    add edx, 96
    cmp edx, 255
    jbe .store
    mov edx, 255
.store:
    mov [snn_activation + eax], dl
.done:
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
snn_random            dd 0
snn_spike_count       dd 0
snn_spikes_this_tick  dd 0
snn_energy            dd 0
snn_activation        times SNN_NEURONS db 0
snn_spiked            times SNN_NEURONS db 0
snn_weights           times SNN_NEURONS * SNN_NEURONS db 0

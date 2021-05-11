
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <iron/full.h>
#include <iron/gl.h>
#include <iron/audio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <GL/gl.h>
#include "scheme.h"
#include "scheme-private.h"
#include "u32_to_u32_table.h"
#include "ptr_to_u32_table.h"
#include "main.h"
#include "internal.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <microio.h>
#define ULONG_MAX 0xFFFFFFFF

static inline f64 randf()
{
  static const f64 kTwoOverUlongMax = 2.0f / (f64)ULONG_MAX;
	// Calculate pseudo-random 32 bit number based on linear congruential method.
	// http://www.musicdsp.org/showone.php?id=59
	static unsigned long random = 22222;
	random = (random * 196314165) + 907633515;
	u64 sample = random % (1024);
	return (sample / 1024.0f) * 2.0f - 1.0f;
}

typedef enum{
	     AD_SIN,
	     AD_NOISE,
	     AD_END,
	     AD_LOAD_F32,
	     AD_LOAD_TIME,
	     AD_MIX,
	     AD_AMP,
	     AD_LP,
	     AD_ADSR,
	     AD_EMIT,
	     AD_CALL,
	     AD_MAGIC = 0xAD
}AD_OPCODE;

typedef struct{
  int sample;
  int sample_frequency;
  f32 t;
  binary_io stack;
  binary_io control_stack;
}ad_vm;

f32 ad_popf32(ad_vm * vm){
  ASSERT(io_read_u8(&vm->stack) == AD_MAGIC);
  return io_read_f32(&vm->stack);
}

void ad_pushf32(ad_vm * vm, f32 val){
  io_write_f32(&vm->stack, val);
  io_write_u8(&vm->stack, AD_MAGIC);
}

void io_seek(io_base * io, u64 offset){
  let o = io_offset(io);
  if(o > offset){
    io_rewind(io, o - offset);
  }else{
    io_advance(io, offset - o);
  }
}

void vm_process_audio_code(ad_vm * vm, io_reader * reader){
  f32 t = vm->t;
  while(true){
    
    AD_OPCODE rd = io_read_u8(reader);
    switch(rd){
    case AD_SIN:{
      f32 frequency = ad_popf32(vm);
      f32 val = sin(frequency * 2.0 * M_PI * vm->sample / vm->sample_frequency);
      ad_pushf32(vm, val);
      break;
    }
    case AD_NOISE:{
      f32 val = randf();
      ad_pushf32(vm, val);
      break;
    }
    case AD_MIX:{
      f32 v1 = ad_popf32(vm);
      f32 v2 = ad_popf32(vm);
      ad_pushf32(vm, 0.5f * (v1 + v2));
      break;
    }
    case AD_AMP:{
      f32 v1 = ad_popf32(vm);
      f32 amp = ad_popf32(vm);
      ad_pushf32(vm, v1 * amp);
      break;
    }
    case AD_LOAD_F32:{
      f32 val = io_read_f32(reader);
      ad_pushf32(vm, val);
      break;
    }
    case AD_LOAD_TIME:{
      ad_pushf32(vm, vm->t);
      break;
    }
    case AD_ADSR:{
      f32 v1 = ad_popf32(vm);
      f32 a = io_read_f32(reader);
      f32 d = io_read_f32(reader);
      f32 s = io_read_f32(reader);
      f32 r = io_read_f32(reader);
      f32 amp = 0.0f;
      f32 rest = 0.75;
      if(t < a ){
	amp = t / a;
      }else if(t < a + d){
	f32 t2 = t - a;
	amp = (1 - t2 / d) * (1 - rest) + rest;

      }else if(t < a + d + s){
	f32 t2 = t - a - d;
	amp = rest;
      }else if(t < a + d + s + r){
	f32 t2 = t - a - d - s;
	amp = rest * (1 - t2/r);
      }
      
      ad_pushf32(vm, amp * v1);
      break;
    }
    case AD_EMIT:
      {
	if(io_offset(&vm->control_stack) > 0){
	  
	  u64 offset = io_read_u64(&vm->control_stack);
	  io_seek(reader, offset);
	  break;
	}else{
	  return;
	}
      }
    case AD_CALL:
      {
	u32 offset = io_read_u32(reader);
	
	io_write_u64(&vm->control_stack, io_offset(reader));
	io_seek(reader, offset);
	
	break;
      }
    case AD_END:
      return;
    default:
      ERROR("Invalid opcode %i\n", rd);

    }
  }
}

void vm_fill_samples(ad_vm * vm, io_reader * code, f32 * samples, size_t count, size_t entry_point){
  for(size_t i = 0; i < count; i++){
    io_seek(code, entry_point);
    vm->sample = i;
    vm->t = (f32) i / vm->sample_frequency;

    vm_process_audio_code(vm, code);
    samples[i] = ad_popf32(vm);
    
  }

}

ad_vm ad_new(){
  ad_vm vm = {
	    .sample_frequency = 44100,
	      .sample = 0,
	      .t = 0.0,
	    .stack = {0},
	    .control_stack = {0}
  };
  vm.stack.mode = IO_MODE_STACK;
  vm.control_stack.mode = IO_MODE_STACK;
  return vm;
}

void test_asdr_env(){
  io_reader code_reader = {0};

  io_write_u8(&code_reader, AD_LOAD_F32);
  io_write_f32(&code_reader, 1.0);
  
  io_write_u8(&code_reader, AD_ADSR);
  io_write_f32(&code_reader, 0.25);
  io_write_f32(&code_reader, 0.25);
  io_write_f32(&code_reader, 0.25);
  io_write_f32(&code_reader, 0.25);
  io_write_u8(&code_reader, AD_END);
  io_reset(&code_reader);
  ad_vm vm = ad_new();
  f32 v[300];
  vm.sample_frequency = 256;
  vm_fill_samples(&vm, &code_reader, v, 300, 0);
  for(int i = 0; i < 300; i++){
    //logd("%i %f\n", i, v[i]);
  }
}

typedef struct {
  io_writer state;
  int id;
  size_t entry_point;
  f32 time;
  int beat;
  f32 bps;
}songctx;

songctx * current_song;

size_t sg_sin(f32 frequency){
  var state = &current_song->state;
  var offset = state->offset;
  io_write_u8(state, AD_LOAD_F32);
  io_write_f32(state, frequency);
  io_write_u8(state, AD_SIN);
  io_write_u8(state, AD_EMIT);
  return offset;
}

size_t sg_adsr(f32 a, f32 d, f32 s, f32 r){
  var state = &current_song->state;
  var offset = state->offset;
  io_write_u8(state, AD_ADSR);
  io_write_f32(state, a);
  io_write_f32(state, d);
  io_write_f32(state, s);
  io_write_f32(state, r);
  io_write_u8(state, AD_EMIT);
  return offset;
}
size_t sg_modulate(size_t gen, size_t mod){
  var state = &current_song->state;
  var offset = state->offset;
  
  io_write_u8(state, AD_CALL);
  io_write_u32(state, gen);
  io_write_u8(state, AD_CALL);
  io_write_u32(state, mod);
  io_write_u8(state, AD_EMIT);
  return offset;
}

size_t sg_play(size_t offset){
  var state = &current_song->state;
  var offset2 = state->offset;
  io_write_u8(state, AD_CALL);
  io_write_u32(state, offset);
  io_write_u8(state, AD_EMIT);
  return offset2;
}
void sg_start(size_t offset){
  current_song->entry_point = offset;
}

int get_note(int * array, size_t count){
  int elem = current_song->beat % count;
  return array[elem];
}


typedef struct{
  int notes[12];
  int length;
}scale;

const scale c_scale = {.notes = {0, 2, 4, 5, 7, 9, 10}, .length = 7};

int scale_lookup(int p, const scale s){
  return s.notes[p % s.length];
}

void song_gen1(){
  // immidiate mode song generation
  static int song[] = {0, 0, 0, 5,  0, 3, 2, 5,  0, 2, 5, 3,  2,5,6,7};
  var s1 = get_note(song, array_count(song));
  int note = scale_lookup(s1, c_scale);
  f32 freq = note_to_frequency(s1);
  sg_start(sg_play(sg_modulate(sg_sin(840), sg_adsr(0.1, 0.1, 0.1, 0.05))));
}


void init_audio_subsystem(context * ctx){
  audio_context * audio  = audio_initialize(44100);
  ad_vm vm = ad_new();
  /*io_reader code_reader = {0};
  io_write_u8(&code_reader, AD_LOAD_F32);   
  io_write_f32(&code_reader, 440.0);
  io_write_u8(&code_reader, AD_SIN);
  
  io_write_u8(&code_reader, AD_NOISE);
  io_write_u8(&code_reader, AD_LOAD_F32);
  io_write_f32(&code_reader, 0.125);
  io_write_u8(&code_reader, AD_AMP);
  io_write_u8(&code_reader, AD_MIX);
  io_write_u8(&code_reader, AD_LOAD_F32);
  io_write_f32(&code_reader, 0.25);
  io_write_u8(&code_reader, AD_AMP);
  io_write_u8(&code_reader, AD_ADSR);
  io_write_f32(&code_reader, 0.01);
  io_write_f32(&code_reader, 0.01);
  io_write_f32(&code_reader, 0.21);
  io_write_f32(&code_reader, 0.21);
  io_write_u8(&code_reader, AD_END);
  io_reset(&code_reader);
  */
  songctx c = {.state = {0}, .id = 0,
	       .entry_point = 0, .time = 0.0,
	       .bps = 120, .beat = 0};
  current_song = &c;
  song_gen1();
  io_reset(&c.state);
  for(size_t i = 0; i < c.state.size; i++){
    logd("%x ", io_read_u8(&c.state));
  }
  logd("\n");
  current_song = NULL;
  io_reset(&c.state);
  io_seek(&c.state, c.entry_point);
  int size = 80000;
  f32 * data = alloc0(sizeof(data[0]) * size);
  vm_fill_samples(&vm, &c.state, data, size, c.entry_point);
  var sample = audio_load_samplef(audio, data, size);
  audio_update_streams(audio);
  audio_play_sample(audio, sample);
  ctx->audio = audio;
  test_asdr_env();
}

// $Id$
// -*-C++-*-
// * BeginRiceCopyright *****************************************************
// 
// Copyright ((c)) 2002, Rice University 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// 
// * Neither the name of Rice University (RICE) nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
// 
// This software is provided by RICE and contributors "as is" and any
// express or implied warranties, including, but not limited to, the
// implied warranties of merchantability and fitness for a particular
// purpose are disclaimed. In no event shall RICE or contributors be
// liable for any direct, indirect, incidental, special, exemplary, or
// consequential damages (including, but not limited to, procurement of
// substitute goods or services; loss of use, data, or profits; or
// business interruption) however caused and on any theory of liability,
// whether in contract, strict liability, or tort (including negligence
// or otherwise) arising in any way out of the use of this software, even
// if advised of the possibility of such damage. 
// 
// ******************************************************* EndRiceCopyright *

//***************************************************************************
//
// File:
//    x86ISA.h
//
// Purpose:
//    [The purpose of this file]
//
// Description:
//    [The set of functions, macros, etc. defined in the file]
//
//***************************************************************************

#ifndef x86ISA_H 
#define x86ISA_H

//************************* System Include Files ****************************

//*************************** User Include Files ****************************

#include <include/general.h>

#include "ISA.hpp"

//*************************** Forward Declarations ***************************

struct disassemble_info;

//****************************************************************************

//***************************************************************************
// x86ISA
//***************************************************************************

// 'x86ISA': Implements the x86 and x86-64 Instruction Set Architecture
// See comments in 'ISA.h'

class x86ISA : public ISA {
public:  
  x86ISA(bool is_x86_64 = false);
  virtual ~x86ISA();
  
  // --------------------------------------------------------
  // Instructions:
  // --------------------------------------------------------  
  
  virtual ushort GetInstSize(MachInst* mi);
  
  virtual ushort GetInstNumOps(MachInst* mi) { return 1; }

  virtual InstDesc GetInstDesc(MachInst* mi, ushort opIndex, ushort sz = 0);

  virtual Addr GetInstTargetAddr(MachInst* mi, Addr pc, ushort opIndex,
				 ushort sz = 0);
  
  virtual ushort GetInstNumDelaySlots(MachInst* mi, ushort opIndex,
				      ushort sz = 0)
  { return 0; }

  virtual bool IsParallelWithSuccessor(MachInst* mi1, ushort opIndex1,
				       ushort sz1,
				       MachInst* mi2, ushort opIndex2,
				       ushort sz2) const
  { return false; }

private: 
  // Should not be used
  x86ISA(const x86ISA& i) { }
  x86ISA& operator=(const x86ISA& i) { return *this; }

protected:
private:
  bool mIs_x86_64;
  struct disassemble_info *di;
};

//****************************************************************************

#endif 

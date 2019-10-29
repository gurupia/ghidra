/* ###
 * IP: GHIDRA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/// \file translate.hh
/// \brief Classes for disassembly and pcode generation
///
/// Classes for keeping track of spaces and registers (for a single architecture).

#ifndef __CPUI_TRANSLATE__
#define __CPUI_TRANSLATE__

#include "pcoderaw.hh"
#include "float.hh"

// Some errors specific to the translation unit

/// \brief Exception for encountering unimplemented pcode
///
/// This error is thrown when a particular machine instruction
/// cannot be translated into pcode. This particular error
/// means that the particular instruction being decoded was valid,
/// but the system doesn't know how to represent it in pcode.
struct UnimplError : public LowlevelError {
  int4 instruction_length;	///< Number of bytes in the unimplemented instruction
  /// \brief Constructor
  ///
  /// \param s is a more verbose description of the error
  /// \param l is the length (in bytes) of the unimplemented instruction
  UnimplError(const string &s,int4 l) : LowlevelError(s) { instruction_length = l; }
};

/// \brief Exception for bad instruction data
///
/// This error is thrown when the system cannot decode data
/// for a particular instruction.  This usually means that the
/// data is not really a machine instruction, but may indicate
/// that the system is unaware of the particular instruction.
struct BadDataError : public LowlevelError {
  /// \brief Constructor
  ///
  /// \param s is a more verbose description of the error
  BadDataError(const string &s) : LowlevelError(s) {}
};

class Translate;

/// \brief Object for describing how a space should be truncated
///
/// This can turn up in various XML configuration files and essentially acts
/// as a command to override the size of an address space as defined by the architecture
class TruncationTag {
  string spaceName;	///< Name of space to be truncated
  uint4 size;		///< Size truncated addresses into the space
public:
  void restoreXml(const Element *el);				///< Restore \b this from XML
  const string &getName(void) const { return spaceName; }	///< Get name of address space being truncated
  uint4 getSize(void) const { return size; }			///< Size (of pointers) for new truncated space
};

/// \brief Abstract class for emitting pcode to an application
///
/// Translation engines pass back the generated pcode for an
/// instruction to the application using this class.
class PcodeEmit {
public:
  virtual ~PcodeEmit(void) {}	///< Virtual destructor

  /// \brief The main pcode emit method.
  ///
  /// A single pcode instruction is returned to the application
  /// via this method.  Particular applications override it
  /// to tailor how the operations are used.
  /// \param addr is the Address of the machine instruction
  /// \param opc is the opcode of the particular pcode instruction
  /// \param outvar if not \e null is a pointer to data about the
  ///               output varnode
  /// \param vars is a pointer to an array of VarnodeData for each
  ///             input varnode
  /// \param isize is the number of input varnodes
  virtual void dump(const Address &addr,OpCode opc,VarnodeData *outvar,VarnodeData *vars,int4 isize)=0;

  /// Emit pcode directly from an XML tag
  void restoreXmlOp(const Element *el,const AddrSpaceManager *trans);

  enum {			// Tags for packed pcode format
    unimpl_tag = 0x20,
    inst_tag = 0x21,
    op_tag = 0x22,
    void_tag = 0x23,
    spaceid_tag = 0x24,
    addrsz_tag = 0x25,
    end_tag = 0x60
  };
  /// Helper function for unpacking an offset from a pcode byte stream
  static const uint1 *unpackOffset(const uint1 *ptr,uintb &off);
  /// Helper function for unpacking a varnode from a pcode byte stream
  static const uint1 *unpackVarnodeData(const uint1 *ptr,VarnodeData &v,const AddrSpaceManager *trans);
  /// Emit pcode directly from a packed byte stream
  const uint1 *restorePackedOp(const Address &addr,const uint1 *ptr,const AddrSpaceManager *trans);
};

/// \brief Abstract class for emitting disassembly to an application
///
/// Translation engines pass back the disassembly character data
/// for decoded machine instructions to an application using this class.
class AssemblyEmit {
public:
  virtual ~AssemblyEmit(void) {} ///< Virtual destructor

  /// \brief The main disassembly emitting method.
  ///
  /// The disassembly strings for a single machine instruction
  /// are passed back to an application through this method.
  /// Particular applications can tailor the use of the disassembly
  /// by overriding this method.
  /// \param addr is the Address of the machine instruction
  /// \param mnem is the decoded instruction mnemonic
  /// \param body is the decode body (or operands) of the instruction
  virtual void dump(const Address &addr,const string &mnem,const string &body)=0;
};

/// \brief Abstract class for converting native constants to addresses
///
/// This class is used if there is a special calculation to get from a constant embedded
/// in the code being analyzed to the actual Address being referred to.  This is used especially
/// in the case of a segmented architecture, where "near" pointers must be extended to a full address
/// with implied segment information.
class AddressResolver {
public:
  virtual ~AddressResolver(void) {} ///> Virtual destructor

  /// \brief The main resolver method.
  ///
  /// Given a native constant in a specific context, resolve what address is being referred to.
  /// \param val is constant to be resolved to an address
  /// \param sz is the size of \e val in context.
  /// \param point is the address at which this constant is being used
  /// \param fullEncoding is used to hold the full pointer encoding if \b val is a partial encoding
  /// \return the resolved Address
  virtual Address resolve(uintb val,int4 sz,const Address &point,uintb &fullEncoding)=0;
};

/// \brief A virtual space \e stack space
///
/// In a lot of analysis situations it is convenient to extend
/// the notion of an address space to mean bytes that are indexed
/// relative to some base register.  The canonical example of this
/// is the \b stack space, which models the concept of local
/// variables stored on the stack.  An address of (\b stack, 8)
/// might model the address of a function parameter on the stack
/// for instance, and (\b stack, 0xfffffff4) might be the address
/// of a local variable.  A space like this is inherently \e virtual
/// and contained within whatever space is being indexed into.
class SpacebaseSpace : public AddrSpace {
  friend class AddrSpaceManager;
  AddrSpace *contain;		///< Containing space
  bool hasbaseregister;		///< true if a base register has been attached
  bool isNegativeStack;		///< true if stack grows in negative direction
  VarnodeData baseloc;		///< location data of the base register
  VarnodeData baseOrig;		///< Original base register before any truncation
  void setBaseRegister(const VarnodeData &data,int4 origSize,bool stackGrowth); ///< Set the base register at time space is created
public:
  SpacebaseSpace(AddrSpaceManager *m,const Translate *t,const string &nm,int4 ind,int4 sz,AddrSpace *base,int4 dl);
  SpacebaseSpace(AddrSpaceManager *m,const Translate *t); ///< For use with restoreXml
  virtual int4 numSpacebase(void) const;
  virtual const VarnodeData &getSpacebase(int4 i) const;
  virtual const VarnodeData &getSpacebaseFull(int4 i) const;
  virtual bool stackGrowsNegative(void) const { return isNegativeStack; }
  virtual AddrSpace *getContain(void) const { return contain; } ///< Return containing space
  virtual void saveXml(ostream &s) const;
  virtual void restoreXml(const Element *el);
};

/// \brief A record describing how logical values are split
/// 
/// The decompiler can describe a logical value that is stored split across multiple
/// physical memory locations.  This record describes such a split. The pieces must be listed
/// from \e most \e significant to \e least \e significant.
class JoinRecord {
  friend class AddrSpaceManager;
  vector<VarnodeData> pieces;	///< All the physical pieces of the symbol
  VarnodeData unified; ///< Special entry representing entire symbol in one chunk
public:
  int4 numPieces(void) const { return pieces.size(); }	///< Get number of pieces in this record
  bool isFloatExtension(void) const { return (pieces.size() == 1); }	///< Does this record extend a float varnode
  const VarnodeData &getPiece(int4 i) const { return pieces[i]; }	///< Get the i-th piece
  const VarnodeData &getUnified(void) const { return unified; }		///< Get the Varnode whole
  bool operator<(const JoinRecord &op2) const; ///< Compare records lexigraphically by pieces
};

/// \brief Comparator for JoinRecord objects
struct JoinRecordCompare {
  bool operator()(const JoinRecord *a,const JoinRecord *b) const {
    return *a < *b; }		///< Compare to JoinRecords using their built-in comparison
};

/// \brief A manager for different address spaces
///
/// Allow creation, lookup by name, lookup by shortcut, lookup by name, and iteration
/// over address spaces
class AddrSpaceManager {
  vector<AddrSpace *> baselist; ///< Every space we know about for this architecture
  vector<AddressResolver *> resolvelist; ///< Special constant resolvers
  map<string,AddrSpace *> name2Space;	///< Map from name -> space
  map<int4,AddrSpace *> shortcut2Space;	///< Map from shortcut -> space
  AddrSpace *constantspace;	///< Quick reference to constant space
  AddrSpace *defaultspace;	///< Generally primary RAM, where assembly pointers point to
  AddrSpace *iopspace;		///< Space for internal pcode op pointers
  AddrSpace *fspecspace;	///< Space for internal callspec pointers
  AddrSpace *joinspace;		///< Space for unifying split variables
  AddrSpace *stackspace;	///< Stack space associated with processor
  AddrSpace *uniqspace;		///< Temporary space associated with processor
  uintb joinallocate;		///< Next offset to be allocated in join space
  set<JoinRecord *,JoinRecordCompare> splitset;	///< Different splits that have been defined in join space
  vector<JoinRecord *> splitlist; ///< JoinRecords indexed by join address
protected:
  AddrSpace *restoreXmlSpace(const Element *el,const Translate *trans); ///< Add a space to the model based an on XML tag
  void restoreXmlSpaces(const Element *el,const Translate *trans); ///< Restore address spaces in the model from an XML tag
  void setDefaultSpace(int4 index); ///< Set the default address space
  void setReverseJustified(AddrSpace *spc); ///< Set reverse justified property on this space
  void assignShortcut(AddrSpace *spc);	///< Select a shortcut character for a new space
  void insertSpace(AddrSpace *spc); ///< Add a new address space to the model
  void copySpaces(const AddrSpaceManager *op2);	///< Copy spaces from another manager
  void addSpacebasePointer(SpacebaseSpace *basespace,const VarnodeData &ptrdata,int4 truncSize,bool stackGrowth); ///< Set the base register of a spacebase space
  void insertResolver(AddrSpace *spc,AddressResolver *rsolv); ///< Override the base resolver for a space
public:
  AddrSpaceManager(void);	///< Construct an empty address space manager
  virtual ~AddrSpaceManager(void); ///< Destroy the manager
  int4 getDefaultSize(void) const; ///< Get size of addresses for the default space
  AddrSpace *getSpaceByName(const string &nm) const; ///< Get address space by name
  AddrSpace *getSpaceByShortcut(char sc) const;	///< Get address space from its shortcut
  AddrSpace *getIopSpace(void) const; ///< Get the internal pcode op space
  AddrSpace *getFspecSpace(void) const; ///< Get the internal callspec space
  AddrSpace *getJoinSpace(void) const; ///< Get the joining space
  AddrSpace *getStackSpace(void) const; ///< Get the stack space for this processor
  AddrSpace *getUniqueSpace(void) const; ///< Get the temporary register space for this processor
  AddrSpace *getDefaultSpace(void) const; ///< Get the default address space of this processor
  AddrSpace *getConstantSpace(void) const; ///< Get the constant space
  Address getConstant(uintb val) const; ///< Get a constant encoded as an Address
  Address createConstFromSpace(AddrSpace *spc) const; ///< Create a constant address encoding an address space
  Address resolveConstant(AddrSpace *spc,uintb val,int4 sz,const Address &point,uintb &fullEncoding) const;
  int4 numSpaces(void) const; ///< Get the number of address spaces for this processor
  AddrSpace *getSpace(int4 i) const; ///< Get an address space via its index
  AddrSpace *getNextSpaceInOrder(AddrSpace *spc) const; ///< Get the next \e contiguous address space
  JoinRecord *findAddJoin(const vector<VarnodeData> &pieces,uint4 logicalsize); ///< Get (or create) JoinRecord for \e pieces
  JoinRecord *findJoin(uintb offset) const; ///< Find JoinRecord for \e offset in the join space
  void setDeadcodeDelay(AddrSpace *spc,int4 delaydelta); ///< Set the deadcodedelay for a specific space
  void truncateSpace(const TruncationTag &tag);	///< Mark a space as truncated from its original size

  /// \brief Build a logically lower precision storage location for a bigger floating point register
  Address constructFloatExtensionAddress(const Address &realaddr,int4 realsize,int4 logicalsize);

  /// \brief Build a logical whole from register pairs
  Address constructJoinAddress(const Translate *translate,const Address &hiaddr,int4 hisz,const Address &loaddr,int4 losz);
};

/// \brief The interface to a translation engine for a processor.
///
/// This interface performs translations of instruction data
/// for a particular processor.  It has two main functions
///     - Disassemble single machine instructions
///     - %Translate single machine instructions into \e pcode.
///
/// It is also the repository for information about the exact
/// configuration of the reverse engineering model associated
/// with the processor. In particular, it knows about all the
/// address spaces, registers, and spacebases for the processor.
class Translate : public AddrSpaceManager {
  bool target_isbigendian;	///< \b true if the general endianness of the process is big endian
  uintm unique_base;		///< Starting offset into unique space
protected:
  int4 alignment;      ///< Byte modulo on which instructions are aligned
  vector<FloatFormat> floatformats; ///< Floating point formats utilized by the processor

  void setBigEndian(bool val);	///< Set general endianness to \b big if val is \b true
  void setUniqueBase(uintm val); ///< Set the base offset for new temporary registers
public:
  Translate(void); 		///< Constructor for the translator
  void setDefaultFloatFormats(void); ///< If no explicit float formats, set up default formats
  bool isBigEndian(void) const; ///< Is the processor big endian?
  const FloatFormat *getFloatFormat(int4 size) const; ///< Get format for a particular floating point encoding
  int4 getAlignment(void) const; ///< Get the instruction alignment for the processor
  uintm getUniqueBase(void) const; ///< Get the base offset for new temporary registers

  /// \brief Initialize the translator given XML configuration documents
  ///
  /// A translator gets initialized once, possibly using XML documents
  /// to configure it.
  /// \param store is a set of configuration documents
  virtual void initialize(DocumentStorage &store)=0;

  /// \brief Add a new context variable to the model for this processor
  ///
  /// Add the name of a context register used by the processor and
  /// how that register is packed into the context state. This
  /// information is used by a ContextDatabase to associate names
  /// with context information and to pack context into a single
  /// state variable for the translation engine.
  /// \param name is the name of the new context variable
  /// \param sbit is the first bit of the variable in the packed state
  /// \param ebit is the last bit of the variable in the packed state
  virtual void registerContext(const string &name,int4 sbit,int4 ebit) {}

  /// \brief Set the default value for a particular context variable
  ///
  /// Set the value to be returned for a context variable when
  /// there are no explicit address ranges specifying a value
  /// for the variable.
  /// \param name is the name of the context variable
  /// \param val is the value to be considered default
  virtual void setContextDefault(const string &name,uintm val) {}

  /// \brief Toggle whether disassembly is allowed to affect context
  ///
  /// By default the disassembly/pcode translation engine can change
  /// the global context, thereby affecting later disassembly.  Context
  /// may be getting determined by something other than control flow in,
  /// the disassembly, in which case this function can turn off changes
  /// made by the disassembly
  /// \param val is \b true to allow context changes, \b false prevents changes
  virtual void allowContextSet(bool val) const {}

  /// \brief Add a named register to the model for this processor
  ///
  /// \deprecated All registers used to be formally added to the
  /// processor model through this method.
  /// \param nm is the name of the new register
  /// \param base is the address space containing the register
  /// \param offset is the offset of the register
  /// \param size is the number of bytes in the register
  virtual void addRegister(const string &nm,AddrSpace *base,uintb offset,int4 size)=0;

  /// \brief Get a register as VarnodeData given its name
  ///
  /// Retrieve the location and size of a register given its name
  /// \param nm is the name of the register
  /// \return the VarnodeData for the register
  virtual const VarnodeData &getRegister(const string &nm) const=0;

  /// \brief Get the name of a register given its location
  ///
  /// Generic references to locations in a \e register space can
  /// be translated into the associated register \e name.  If the
  /// location doesn't match a register \e exactly, an empty string
  /// is returned.
  /// \param base is the address space containing the location
  /// \param off is the offset of the location
  /// \param size is the size of the location
  /// \return the name of the register, or an empty string
  virtual string getRegisterName(AddrSpace *base,uintb off,int4 size) const=0;

  /// \brief Get a list of all register names and the corresponding location
  ///
  /// Most processors have a list of named registers and possibly other memory locations
  /// that are specific to it.  This function populates a map from the location information
  /// to the name, for every named location known by the translator
  /// \param reglist is the map which will be populated by the call
  virtual void getAllRegisters(map<VarnodeData,string> &reglist) const=0;

  /// \brief Get a list of all \e user-defined pcode ops
  ///
  /// The pcode model allows processors to define new pcode
  /// instructions that are specific to that processor. These
  /// \e user-defined instructions are all identified by a name
  /// and an index.  This method returns a list of these ops
  /// in index order.
  /// \param res is the resulting vector of user op names
  virtual void getUserOpNames(vector<string> &res) const=0;

  /// \brief Get the length of a machine instruction
  ///
  /// This method decodes an instruction at a specific address
  /// just enough to find the number of bytes it uses within the
  /// instruction stream.
  /// \param baseaddr is the Address of the instruction
  /// \return the number of bytes in the instruction
  virtual int4 instructionLength(const Address &baseaddr) const=0;

  /// \brief Transform a single machine instruction into pcode
  ///
  /// This is the main interface to the pcode translation engine.
  /// The \e dump method in the \e emit object is invoked exactly
  /// once for each pcode operation in the translation for the
  /// machine instruction at the given address.
  /// This routine can throw either
  ///     - UnimplError or
  ///     - BadDataError
  ///
  /// \param emit is the tailored pcode emitting object
  /// \param baseaddr is the Address of the machine instruction
  /// \return the number of bytes in the machine instruction
  virtual int4 oneInstruction(PcodeEmit &emit,const Address &baseaddr) const=0;

  /// \brief Disassemble a single machine instruction
  ///
  /// This is the main interface to the disassembler for the
  /// processor.  It disassembles a single instruction and
  /// returns the result to the application via the \e dump
  /// method in the \e emit object.
  /// \param emit is the disassembly emitting object
  /// \param baseaddr is the address of the machine instruction to disassemble
  virtual int4 printAssembly(AssemblyEmit &emit,const Address &baseaddr) const=0;
};

/// Return the size of addresses for the processor's official
/// default space. This space is usually the main RAM databus.
/// \return the size of an address in bytes
inline int4 AddrSpaceManager::getDefaultSize(void) const {
  return defaultspace->getAddrSize();
}

/// There is a special address space reserved for encoding pointers
/// to pcode operations as addresses.  This allows a direct pointer
/// to be \e hidden within an operation, when manipulating pcode
/// internally. (See IopSpace)
/// \return a pointer to the address space
inline AddrSpace *AddrSpaceManager::getIopSpace(void) const {
  return iopspace;
}

/// There is a special address space reserved for encoding pointers
/// to the FuncCallSpecs object as addresses. This allows direct
/// pointers to be \e hidden within an operation, when manipulating
/// pcode internally. (See FspecSpace)
/// \return a pointer to the address space
inline AddrSpace *AddrSpaceManager::getFspecSpace(void) const {
  return fspecspace;
}

/// There is a special address space reserved for providing a 
/// logical contiguous memory location for variables that are
/// really split between two physical locations.  This allows the
/// the decompiler to work with the logical value. (See JoinSpace)
/// \return a pointer to the address space
inline AddrSpace *AddrSpaceManager::getJoinSpace(void) const {
  return joinspace;
}

/// Most processors have registers and instructions that are
/// reserved for implementing a stack. In the pcode translation,
/// these are translated into locations and operations on a
/// dedicated \b stack address space. (See SpacebaseSpace)
/// \return a pointer to the \b stack space
inline AddrSpace *AddrSpaceManager::getStackSpace(void) const {
  return stackspace;
}

/// Both the pcode translation process and the simplification
/// process need access to a pool of temporary registers that
/// can be used for moving data around without affecting the
/// address spaces used to formally model the processor's RAM
/// and registers.  These temporary locations are all allocated
/// from a dedicated address space, referred to as the \b unique
/// space. (See UniqueSpace)
/// \return a pointer to the \b unique space
inline AddrSpace *AddrSpaceManager::getUniqueSpace(void) const {
  return uniqspace;
}

/// Most processors have a main address bus, on which the bulk
/// of the processor's RAM is mapped.  Everything referenced
/// with this address bus should be modeled in pcode with a
/// single address space, referred to as the \e default space.
/// \return a pointer to the \e default space
inline AddrSpace *AddrSpaceManager::getDefaultSpace(void) const {
  return defaultspace;
}

/// Pcode represents constant values within an operation as
/// offsets within a special \e constant address space. 
/// (See ConstantSpace)
/// \return a pointer to the \b constant space
inline AddrSpace *AddrSpaceManager::getConstantSpace(void) const {
  return constantspace;
}

/// This routine encodes a specific value as a \e constant
/// address. I.e. the address space of the resulting Address
/// will be the \b constant space, and the offset will be the
/// value.
/// \param val is the constant value to encode
/// \return the \e constant address
inline Address AddrSpaceManager::getConstant(uintb val) const {
  return Address(constantspace,val);
}

/// This routine is used to encode a pointer to an address space
/// as a \e constant Address, for use in \b LOAD and \b STORE
/// operations.  This is used internally and is slightly more
/// efficient than storing the formal index of the space
/// param spc is the space pointer to be encoded
/// \return the encoded Address
inline Address AddrSpaceManager::createConstFromSpace(AddrSpace *spc) const {
  return Address(constantspace,(uintb)(uintp)spc);
}

/// This returns the total number of address spaces used by the
/// processor, including all special spaces, like the \b constant
/// space and the \b iop space. 
/// \return the number of spaces
inline int4 AddrSpaceManager::numSpaces(void) const {
  return baselist.size();
}

/// This retrieves a specific address space via its formal index.
/// All spaces have an index, and in conjunction with the numSpaces
/// method, this method can be used to iterate over all spaces.
/// \param i is the index of the address space
/// \return a pointer to the desired space
inline AddrSpace *AddrSpaceManager::getSpace(int4 i) const {
  return baselist[i];
}

/// Although endianness is usually specified on the space, most languages set an endianness
/// across the entire processor.  This routine sets the endianness to \b big if the -val-
/// is passed in as \b true. Otherwise, the endianness is set to \b small.
/// \param val is \b true if the endianness should be set to \b big
inline void Translate::setBigEndian(bool val) {
  target_isbigendian = val; 
}

/// The \e unique address space, for allocating temporary registers,
/// is used for both registers needed by the pcode translation
/// engine and, later, by the simplification engine.  This routine
/// sets the boundary of the portion of the space allocated
/// for the pcode engine, and sets the base offset where registers
/// created by the simplification process can start being allocated.
/// \param val is the boundary offset
inline void Translate::setUniqueBase(uintm val) {
  if (val>unique_base) unique_base = val;
}

/// Processors can usually be described as using a big endian
/// encoding or a little endian encoding. This routine returns
/// \b true if the processor globally uses big endian encoding.
/// \return \b true if big endian
inline bool Translate::isBigEndian(void) const {
  return target_isbigendian;
}

/// If machine instructions need to have a specific alignment
/// for this processor, this routine returns it. I.e. a return
/// value of 4, means that the address of all instructions
/// must be a multiple of 4. If there is no
/// specific alignment requirement, this routine returns 1.
/// \return the instruction alignment
inline int4 Translate::getAlignment(void) const {
  return alignment;
}

/// This routine gets the base offset, within the \e unique
/// temporary register space, where new registers can be
/// allocated for the simplification process.  Locations before
/// this offset are reserved registers needed by the pcode
/// translation engine.
/// \return the first allocatable offset
inline uintm Translate::getUniqueBase(void) const {
  return unique_base;
}

#endif

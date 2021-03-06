#pragma once

#include <bitset>

#include "pywrap/compat/macros.hpp"

#include "hal/Coordinate/geometry.h"
#include "hal/strong_typedef.h"
#include "hal/HMFCommon.h"
#include "hal/Coordinate/HMFGeometry.h"

#include "hal/HICANN/STDPControl.h"
#include "hal/HICANN/DriverDecoder.h"
#include "hal/HICANN/SynapseDecoder.h"
#include "hal/HICANN/L1Address.h"
#include "hal/HICANN/Merger.h"
#include "hal/HICANN/FGRow.h"

namespace HMF {

namespace Handle{
	class HICANNHw;
}

namespace HICANN {

STRONG_TYPEDEF_NO_CONSTEXPR(FGRow4, std::array<FGRow BOOST_PP_COMMA() 4>)

STRONG_TYPEDEF_CONSTEXPR(FGRowOnFGBlock4,
                         std::array<Coordinate::FGRowOnFGBlock BOOST_PP_COMMA() 4>,
                         PYPP_CONSTEXPR)

STRONG_TYPEDEF_CONSTEXPR(CrossbarRow, std::array<bool BOOST_PP_COMMA() 4>, PYPP_CONSTEXPR)

STRONG_TYPEDEF_CONSTEXPR(SynapseSwitchRow, std::array<bool BOOST_PP_COMMA() 4 * 4>,
                         PYPP_CONSTEXPR)

class SynapseWeight :
	public Coordinate::detail::RantWrapper<SynapseWeight, uint8_t, 15, 0>
{
public:
	explicit PYPP_CONSTEXPR SynapseWeight(uint8_t val = 0) : rant_t(val) {}
	static SynapseWeight from_bitset(std::bitset<4> const& val) {
		return SynapseWeight(val.to_ulong());
	}

	std::bitset<4> format() const;
};

STRONG_TYPEDEF_CONSTEXPR(WeightRow, std::array<SynapseWeight BOOST_PP_COMMA() 256>,
                         PYPP_CONSTEXPR)

STRONG_TYPEDEF_CONSTEXPR(DecoderRow, std::array<SynapseDecoder BOOST_PP_COMMA() 256>,
                         PYPP_CONSTEXPR)

STRONG_TYPEDEF_CONSTEXPR(DecoderDoubleRow, std::array<DecoderRow BOOST_PP_COMMA() 2>,
                         PYPP_CONSTEXPR)

class BkgRandomISI
    : public Coordinate::detail::RantWrapper<BkgRandomISI, size_t, 32768, 4> {
public:
	explicit PYPP_CONSTEXPR BkgRandomISI(size_t val) : rant_t(val) {}
};

class BkgRegularISI
    : public Coordinate::detail::RantWrapper<BkgRegularISI, size_t, 65536, 2> {
public:
	explicit PYPP_CONSTEXPR BkgRegularISI(size_t val) : rant_t(val) {}
};

/** configuration for a HICANN background event generater.*/
struct BackgroundGenerator
{
public:
	static const size_t
		max_isi_random  = BkgRandomISI::max,  //!< maximum inter spike interval for random mode
		max_isi_regular = BkgRegularISI::max, //!< maximum inter spike interval for regular mode
		min_isi_random  = BkgRandomISI::min,  //!< minimum inter spike interval for random mode
		min_isi_regular = BkgRegularISI::min  //!< minimum inter spike interval for regular mode
	;

	PYPP_CONSTEXPR BackgroundGenerator() :
		mEnable(false),
		mRandom(false),
		mSeed(0),
		mPeriod(100),
		mAddress(0)
	{}

	bool enable() const {return mEnable;}
	void enable(bool value) { mEnable = value;}

	bool random() const {return mRandom;}
	void random(bool value) { mRandom = value;}

	uint16_t seed() const {return mSeed;}
	void seed(uint16_t value) { mSeed = value;}

	uint16_t period() const {return mPeriod;}
	void period(uint16_t value) { mPeriod = value;}

	const L1Address address() const {return mAddress;}
	void address(L1Address value) { mAddress = value;}

	//seed is not gettable from HW that is why it is not compared here
	bool operator ==(BackgroundGenerator const& b) const {
		return (mEnable==b.mEnable && mRandom==b.mRandom
			&& mPeriod==b.mPeriod && mAddress==b.mAddress);
	}

	// https://brainscales-r.kip.uni-heidelberg.de/projects/tns/wiki/PoissonSources
	void set_mode(BkgRandomISI isi);

	void set_mode(BkgRegularISI isi);

private:

	/** set the mode and (mean) inter spike interval in number of clock cycles.
	 *  @param random pseudo-random (true) or regular(false) mode
	 *  @param isi inter spike interval in number of clock cycles. mean isi for random=true.
	 *      range: (2, 65535) for random = false
	 *      range: (4, 32768) for random = true
	 *
	 * */
	void set_mode(bool random, uint32_t isi);

	bool mEnable;  //!< enable backround event generator
	bool mRandom;  //!< pseudo-random (true) or regular(false) mode

	uint16_t mSeed;   //!< seed for the LFSR register, that is used when starting the event generator
	uint16_t mPeriod; //!< period register entry
	L1Address mAddress; //!< 6-bit spl1 address (values 0..63)

	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, unsigned int const)
	{
		using namespace boost::serialization;
		ar & make_nvp("enable", mEnable)
		   & make_nvp("random", mRandom)
		   & make_nvp("seed", mSeed)
		   & make_nvp("period", mPeriod)
		   & make_nvp("address", mAddress);
	}

	friend std::ostream& operator<< (std::ostream& o, BackgroundGenerator const& b);
};

STRONG_TYPEDEF_CONSTEXPR(BackgroundGeneratorArray,
                         std::array<BackgroundGenerator BOOST_PP_COMMA() 8>,
                         PYPP_CONSTEXPR)


/** struct holding the configuration of an individual hardware neuron (Denmem).*/
struct Neuron
{
public:
	// neuron address
	typedef L1Address address_t; //spl1 address type

	Neuron();

	address_t address() const { return _address; }
	void address(address_t a)  { _address = a; }

	// controls comparator, if set to false. Neuron cannot fire and membrane
	// will never be reset (free membrane).
	bool activate_firing() const { return _activate_firing; }
	void activate_firing(bool value)  { _activate_firing = value; }

	// controls whether spl1 address gets emitted or not. For actual spl1 events
	// to be emitted activate_firing also needs to be emitted.
	bool enable_spl1_output() const { return _enable_spl1_output; }
	void enable_spl1_output(bool v) { _enable_spl1_output = v; }

	/** @brief set denmem to receive input from adjacent denmem in neighboring neuron quad
	 *
	 * This is a directed connection. The membrane are shorted, but the spike
	 * signal (necessary for reset) propagates only in one direction.
	 * Depending on whether this is the left/right denmem (in the Quad), it will
	 * receive input from left/right.
	 *
	 * @note don't set the fire_input on both adjacent neurons in neighboring
	 * neuron quads, this might damage hardware.
	 * @note switches responsible for connections within a NeuronQuad are part
	 * of the NeuronQuad data structure
	 */
	bool enable_fire_input() const { return _enable_fire_input; }
	/** @brief does denmem receive input from adjacent denmem in neighboring neuron quad?
	 *
	 * This is a directed connection. The membrane are shorted, but the spike
	 * signal (necessary for reset) propagates only in one direction.
	 * Depending on whether this is the left/right denmem (in the Quad), it will
	 * receive input from left/right.
	 *
	 * @note don't set the fire_input on both adjacent neurons in neighboring
	 * neuron quads, this might damage hardware.
	 * @note switches responsible for connections within a NeuronQuad are part
	 * of the NeuronQuad data structure
	 */
	void enable_fire_input(bool value) { _enable_fire_input = value; }

	bool enable_aout() const { return _enable_aout; }
	void enable_aout(bool value) { _enable_aout = value; }

	void enable_current_input(bool value) { _enable_current_input = value; }
	bool enable_current_input() const { return _enable_current_input; }

	bool operator==(Neuron const& b) const;
	bool operator!=(Neuron const& b) const;

	friend std::ostream& operator<< (std::ostream& os, Neuron const& n);


private:
	//general configuration bits
	address_t _address;
	bool _activate_firing;
	bool _enable_spl1_output;
	bool _enable_fire_input; //input from the left for even neurons and from the right for odd

	//input/output configuration: WARNING: not all permutations are valid!!!
	bool _enable_aout;
	bool _enable_current_input;

	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, unsigned int const)
	{
		using namespace boost::serialization;
		ar & make_nvp("address", _address)
		   & make_nvp("activate_firing", _activate_firing)
		   & make_nvp("enable_spl1_output", _enable_spl1_output)
		   & make_nvp("enable_fire_input", _enable_fire_input)
		   & make_nvp("enable_aout", _enable_aout)
		   & make_nvp("enable_current_input", _enable_current_input);
	}
};


/**
 * template class for a neuron slice.
 */
struct NeuronQuad
{
	typedef HMF::Coordinate::NeuronOnQuad coord_t;

private:
	static const size_t num_cols = coord_t::x_type::end;
	static const size_t num_rows = coord_t::y_type::end;
public:

	typedef std::array<std::array<Neuron, num_cols>, num_rows> type;
	typedef size_t size_type;

	NeuronQuad();

	static PYPP_CONSTEXPR size_type width()  { return num_cols; }
	static PYPP_CONSTEXPR size_type height() { return num_rows; }
	static PYPP_CONSTEXPR size_type size()   { return num_cols * num_rows; }

	Neuron&       operator[](coord_t const& n);
	Neuron const& operator[](coord_t const& n) const;

	/**
	 * @brief connect the @a x th top denmem with bottom denmem in this Quad
	 *
	 * @note to connect denmems beyond NeuronQuad boundaries look for @a
	 * fire_input in HMF::Neuron.
	 */
	void setVerticalInterconnect(coord_t::x_type x, bool value);
	/**
	 * @brief connect the @a x th top denmem with bottom denmem in this Quad
	 *
	 * @note to connect denmems beyond NeuronQuad boundaries look for @a
	 * fire_input in HMF::Neuron.
	 */
	bool getVerticalInterconnect(coord_t::x_type x) const;

	/**
	 * @brief connect either the two top or bottom denmems in this NeuronQuad.
	 */
	void setHorizontalInterconnect(coord_t::y_type y, bool value);
	/**
	 * @brief connect either the two top or bottom denmems in this NeuronQuad.
	 */
	bool getHorizontalInterconnect(coord_t::y_type y) const;

	static int getHWAddress(coord_t const& n);

	bool operator==(NeuronQuad const& b) const;
	bool operator!=(NeuronQuad const& b) const;

	friend std::ostream& operator<< (std::ostream& os, NeuronQuad const& q);

private:
	// lookup table for logic->hw address mapping
	static std::array<int, num_cols*num_rows> const neuron_mapping;

	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, unsigned int const)
	{
		using namespace boost::serialization;
		ar & make_nvp("vertical", vert_switches)
		   & make_nvp("horizontal", hor_switches)
		   & make_nvp("neuron", mNeuron);
	}

	type mNeuron;
	std::bitset<num_cols> vert_switches;
	std::bitset<num_rows> hor_switches;
};

struct NeuronConfig;
void set_neuron_configIMPL(HMF::Handle::HICANNHw &, NeuronConfig const &);
NeuronConfig get_neuron_configIMPL(HMF::Handle::HICANNHw &);

class SRAMReadDelay
    : public Coordinate::detail::RantWrapper<SRAMReadDelay, size_t, 255, 1> {
public:
	explicit PYPP_CONSTEXPR SRAMReadDelay(size_t val) : rant_t(val) {}
	std::bitset<8> to_bitset() const { return std::bitset<8>(value()); }
};

class SRAMSetupPrecharge
    : public Coordinate::detail::RantWrapper<SRAMSetupPrecharge, size_t, 15, 0> {
public:
	explicit PYPP_CONSTEXPR SRAMSetupPrecharge(size_t val) : rant_t(val) {}
	std::bitset<4> to_bitset() const { return std::bitset<4>(value()); }
};

class SRAMWriteDelay
    : public Coordinate::detail::RantWrapper<SRAMWriteDelay, size_t, 15, 0> {
public:
	explicit PYPP_CONSTEXPR SRAMWriteDelay(size_t val) : rant_t(val) {}
	std::bitset<4> to_bitset() const { return std::bitset<4>(value()); }
};

class SRAMControllerTimings {

public:
	explicit PYPP_CONSTEXPR SRAMControllerTimings(
	    SRAMReadDelay rd = SRAMReadDelay(SRAMReadDelay::max),
	    SRAMSetupPrecharge sp = SRAMSetupPrecharge(SRAMSetupPrecharge::max),
	    SRAMWriteDelay wd = SRAMWriteDelay(SRAMWriteDelay::max))
	    : read_delay(rd), setup_precharge(sp), write_delay(wd) {}

	SRAMReadDelay read_delay;
	SRAMSetupPrecharge setup_precharge;
	SRAMWriteDelay write_delay;

	bool operator==(SRAMControllerTimings const& s) const {
		return (read_delay == s.read_delay) && (setup_precharge == s.setup_precharge) &&
		       (write_delay == s.write_delay);
	}

private:
	friend class boost::serialization::access;
	template <typename Archiver>
	void serialize(Archiver& ar, const unsigned int) {
		using boost::serialization::make_nvp;
		ar & make_nvp("read_delay", read_delay)
		   & make_nvp("setup_precharge", setup_precharge)
		   & make_nvp("write_delay", write_delay);
	}

	friend std::ostream& operator<<(std::ostream& os, SRAMControllerTimings const& o);
};

/**
 * Shared Neuron Configuration.
 * Holds the switches, which are shared for each VerticalSide(geometry::top and geometry::bottom)
 */
struct NeuronConfig
{
public:
	static const size_t number_sides=2;       //top and bottom neurons have different config possibilities

	//correct bit is chosen via geometry::SideVertical (geometry::top or geometry::bottom)
	std::bitset<number_sides> bigcap;         //!< use big capacity for all neurons of the HALF block (default: 1)
	std::bitset<number_sides> slow_I_radapt;  //!< slow bits for I_radapt (default: 0)
	std::bitset<number_sides> fast_I_radapt;  //!< fast bits for I_radapt (default: 0)
	std::bitset<number_sides> slow_I_gladapt; //!< slow bits for I_gladapt (default: 0)
	std::bitset<number_sides> fast_I_gladapt; //!< fast bits for I_gladapt (default: 0)
	std::bitset<number_sides> slow_I_gl;      //!< slow bits for I_gl (default: 0)
	std::bitset<number_sides> fast_I_gl;      //!< fast bits for I_gl (default: 0)

	SRAMControllerTimings timings;

	/// Activate the neuron reset for both denmens
	void activate_neuron_reset();
	/// Deactivate the neuron reset for both denmens
	void deactivate_neuron_reset();
	/// Get neuron reset
	/// @return if true the reset is active
	bool get_neuron_reset() const;

	/// Activate the spl1 reset for both denmens
	void activate_spl1_reset();
	/// Deactivate the spl1 reset for both denmens
	void deactivate_spl1_reset();
	/// Get spl1 reset
	/// @return if true the reset is active
	bool get_spl1_reset() const;

	PYPP_CONSTEXPR NeuronConfig() :
		bigcap(0x3), //both bits 1
		slow_I_radapt(0),
		fast_I_radapt(0),
		slow_I_gladapt(0),
		fast_I_gladapt(0),
		slow_I_gl(0),
		fast_I_gl(0),
		timings(SRAMReadDelay(64), SRAMSetupPrecharge(8), SRAMWriteDelay(8)),
		reset_neuron(false),
		reset_spl1(false)
	{}

	bool operator ==(NeuronConfig const& b) const;

private:
	friend void         HICANN::set_neuron_configIMPL(Handle::HICANNHw &, NeuronConfig const &);
	friend NeuronConfig HICANN::get_neuron_configIMPL(Handle::HICANNHw &);

	bool reset_neuron;
	bool reset_spl1;

	//hardware bit-mapping of the config word
	static const size_t
		ra_fast1 = 0, gla_fast1 = 1, gl_fast1 = 2,
		ra_fast0 = 3, gla_fast0 = 4, gl_fast0 = 5,
		ra_slow1 = 6, gla_slow1 = 7, gl_slow1 = 8,
		ra_slow0 = 9, gla_slow0 = 10, gl_slow0 = 11,
		bigcap1 = 12, bigcap0 = 13, spl1reset = 14,
		neuronreset1 = 15, neuronreset0 = 16;

	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, unsigned int const version)
	{
		using namespace boost::serialization;

		ar & make_nvp("bigcap", bigcap)
		   & make_nvp("slow_I_radapt", slow_I_radapt)
		   & make_nvp("fast_I_radapt", fast_I_radapt)
		   & make_nvp("slow_I_gladapt", slow_I_gladapt)
		   & make_nvp("fast_I_gladapt", fast_I_gladapt)
		   & make_nvp("slow_I_gl", slow_I_gl)
		   & make_nvp("fast_I_gl", fast_I_gl);
		switch (version) {
			case 2:
				ar & make_nvp("timings", timings) &
				     make_nvp("reset_neuron", reset_neuron) &
				     make_nvp("reset_spl1", reset_spl1);
				break;
			case 1:
				ar & make_nvp("reset_neuron", reset_neuron)
				   & make_nvp("reset_spl1", reset_spl1);
				break;
			default:
				reset_neuron = false;
				reset_spl1 = false;
		}
	}
	friend std::ostream& operator<< (std::ostream& os, NeuronConfig const& o);
};



/** Configuration of a base class repeater.
 * A repeater can either be off (IDLE), on (FORWARDING),
 * send events (OUTPUT) or receive events (INPUT)
 */
class Repeater
{
public:
	enum Mode {
		FORWARDING, // normal repeater activity
		IDLE,       // off
		INPUT,      // repeater writes to test ports while forwarding
		INPUTONLY,  // repeater writes to test ports without forwarding
		OUTPUT,     // repeater sends test/SPL1-events on L1 from test ports
		LOOPBACK,    // repeater sends its testoutput to testinput of a repeaterblock
	};

	typedef std::bitset<2> config_type;

	PYPP_CONSTEXPR Repeater(
		config_type ren = config_type(0),
		config_type len = config_type(0),
		Mode mode = IDLE,
		std::bitset<2> dir = 0) :
			mRen(ren), mLen(len), mMode(mode), mDirection(dir) {}

	void setRen(config_type v) { mRen = v; }
	const config_type getRen() const { return mRen; }
	void setLen(config_type v) { mLen = v; }
	const config_type getLen() const { return mLen; }

	Mode getMode() const { return mMode; }
	std::bitset<2> getDirections() const { return mDirection; }
	size_t getActiveTransmitters() const { return mDirection.count(); }
	
	bool operator==(Repeater const& other) const;
	bool operator!=(Repeater const& other) const;
	friend std::ostream& operator<<(std::ostream& os, Repeater const& a);

protected:
	//this has something to do with activating capacitances for crosstalk
	//cancelling, but there is no test results or use suggestions anywhere
	config_type mRen;
	config_type mLen;
	Mode mMode;
	//!< possible sending directions: left/top (mDirection[0]) and right/bottom (mDirection[1])
	std::bitset<2> mDirection;

	void setMode(Mode mode) { mMode = mode; }

	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, unsigned int const)
	{
		ar & boost::serialization::make_nvp("ren", mRen)
		   & boost::serialization::make_nvp("len", mLen)
		   & boost::serialization::make_nvp("mode", mMode)
		   & boost::serialization::make_nvp("directions", mDirection);
	}
};



/** Configuration of a vertical repeater.
 * All vertical repeaters are standard repeaters.
 * A vertical repeater supports the same modes as Repeater.
 * Additionally, its direction can be configured.
 *
 * Use as forwarding repeater:
 *     VerticalRepeater vr;
 *     // set direction to be 'to the bottom'
 *     vr.setForwarding(geometry::bottom)
 *     // or set direction to be 'to the top'
 *     vr.setForwarding(geometry::top)
 *
 * TODO: give an example on how to use INPUT rsp OUTPUT with different settings for direction.
 */
class VerticalRepeater : public Repeater
{
public:
	PYPP_CONSTEXPR VerticalRepeater() : Repeater()
	{}

	bool getTop() const { return mDirection[0]; }
	bool getBottom() const { return mDirection[1]; }

	//!< setting to idle also turns off transmitters
	void setIdle() {
		setMode(IDLE);
		setTop(false);
		setBottom(false); }
	//!< forwarding in a certain direction
	void setForwarding(geometry::SideVertical d) {
		setMode(FORWARDING);
		mDirection = 0;
		mDirection[d] = true; }
	//!< recording input, without transmitting
	void setInput() {
		setMode(INPUTONLY); }
	//!< recording input, while transmitting in certain direction
	void setInput(geometry::SideVertical d) {
		setMode(INPUT);
		mDirection = 0;
		mDirection[d] = true; }
	//!< enables test/SPL1-output, setting output in both directions is ALLOWED
	//!< (function has to be called 2 times with both different arguments)
	void setOutput(geometry::SideVertical d, bool value = true) {
		//reset directions if previous state was not OUTPUT
		if (getMode() != OUTPUT) { setMode(OUTPUT); mDirection = 0; }
		mDirection[d] = value;
		if (!getActiveTransmitters())
			throw std::logic_error("Output requires at least one direction to be activated");
	}
	//!< loopback does not require transmitters
	void setLoopback() {
		setMode(LOOPBACK);
		setTop(false);
		setBottom(false); }

	bool operator==(VerticalRepeater const& r) const;
	bool operator!=(VerticalRepeater const& r) const;
	friend std::ostream& operator<<(std::ostream& os, VerticalRepeater const& a);
protected:
	void setTop(bool value) { mDirection[0] = value; }
	void setBottom(bool value) { mDirection[1] = value; }


	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, unsigned int const)
	{
		using boost::serialization::make_nvp;
		using boost::serialization::base_object;
		ar & make_nvp("repeater",
					  base_object<Repeater>(*this));
	}
};

/** Configuration of a horizontal repeater.
 * A horizontal repeater supports the same modes as Repeater.
 * Additionally, its direction can be configured.
 * A sending repeater supports the same configuration as HorizontalRepeater
 * In contrast to a standard horizontal repater, a sending repeater can transmit
 * events coming from MergerTree, this is the case when OUTPUT mode is enabled 
 *
 * Use as forwarding repeater:
 *     HorizontalRepeater hr;
 *     // set direction to be 'to the right'
 *     hr.setForwarding(geometry::right)
 *     // or set direction to be 'to the left'
 *     hr.setForwarding(geometry::left)
 *
 * Use as sending repeater:
 *     HorizontalRepeater hr;
	bool operator==(HorizontalRepeater const& r) const;
	bool operator!=(HorizontalRepeater const& r) const;
	friend std::ostream& operator<<(std::ostream& os, HorizontalRepeater const& a);

 *     // enable output on this hicann
 *     nr.setOutput(geometry::right);
 *     // enable output on left neighbour hicann
 *     nr.setOutput(geometry::left);
 *     // enable output on this and on left neighbour hicann
 *     nr.setOutput(geometry::left);
 *     nr.setOutput(geometry::right);
 *
 * @note some horizontal repeater are sending repeaters (see coordinate definition
 * for HRepeaterOnHICANNand SendingRepeaterOnHICANN).
 *
 */
class HorizontalRepeater: public Repeater
{
public:
	PYPP_CONSTEXPR HorizontalRepeater() : Repeater()
	{}

	bool getLeft() const { return mDirection[0]; }
	bool getRight() const { return mDirection[1]; }

	//!< setting to idle also turns off transmitters
	void setIdle() {
		setMode(IDLE);
		setLeft(false);
		setRight(false); }
	//!< forwarding in a certain direction
	void setForwarding(geometry::SideHorizontal d) {
		setMode(FORWARDING);
		mDirection = 0;
		mDirection[d] = true; }
	//!< recording input, without transmitting
	void setInput() {
		setMode(INPUTONLY); }
	//!< recording input, while transmitting in certain direction
	void setInput(geometry::SideHorizontal d) {
		setMode(INPUT);
		mDirection = 0;
		mDirection[d] = true; }
	//!< enables test/SPL1-output, setting output in both directions is ALLOWED
	//!< (function has to be called 2 times with both different arguments)
	void setOutput(geometry::SideHorizontal d, bool value = true) {
		//reset directions if previous state was not OUTPUT
		if (getMode() != OUTPUT) { setMode(OUTPUT); mDirection = 0; }
		mDirection[d] = value;
		if (!getActiveTransmitters())
			throw std::logic_error("Output requires at least one direction to be activated");
	}
	//!< loopback does not require transmitters
	void setLoopback() {
		setMode(LOOPBACK);
		setLeft(false);
		setRight(false); }
	
	bool operator==(HorizontalRepeater const& r) const;
	bool operator!=(HorizontalRepeater const& r) const;
	friend std::ostream& operator<<(std::ostream& os, HorizontalRepeater const& a);

protected:
	void setLeft(bool value) { mDirection[0] = value; }
	void setRight(bool value) { mDirection[1] = value; }

	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, unsigned int const)
	{
		using boost::serialization::make_nvp;
		using boost::serialization::base_object;
		ar & make_nvp("repeater",
					  base_object<Repeater>(*this));
	}
};

class SynapseWriteDelay
    : public Coordinate::detail::RantWrapper<SynapseWriteDelay, size_t, 3, 0> {
public:
	explicit PYPP_CONSTEXPR SynapseWriteDelay(size_t val) : rant_t(val) {}
	std::bitset<2> to_bitset() const { return std::bitset<2>(value()); }
};

class SynapseOutputDelay
    : public Coordinate::detail::RantWrapper<SynapseOutputDelay, size_t, 15, 0> {
public:
	explicit PYPP_CONSTEXPR SynapseOutputDelay(size_t val) : rant_t(val) {}
	std::bitset<4> to_bitset() const { return std::bitset<4>(value()); }
};

class SynapseSetupPrecharge
    : public Coordinate::detail::RantWrapper<SynapseSetupPrecharge, size_t, 15, 0> {
public:
	explicit PYPP_CONSTEXPR SynapseSetupPrecharge(size_t val) : rant_t(val) {}
	std::bitset<4> to_bitset() const { return std::bitset<4>(value()); }
};

class SynapseEnableDelay
    : public Coordinate::detail::RantWrapper<SynapseEnableDelay, size_t, 15, 0> {
public:
	explicit PYPP_CONSTEXPR SynapseEnableDelay(size_t val) : rant_t(val) {}
	std::bitset<4> to_bitset() const { return std::bitset<4>(value()); }
};

class SynapseControllerTimings {
public:
	explicit PYPP_CONSTEXPR SynapseControllerTimings(
	    SynapseWriteDelay wrdel = SynapseWriteDelay(SynapseWriteDelay::max),
	    SynapseOutputDelay oedel = SynapseOutputDelay(SynapseOutputDelay::max),
	    SynapseSetupPrecharge predel = SynapseSetupPrecharge(SynapseSetupPrecharge::max),
	    SynapseEnableDelay endel = SynapseEnableDelay(SynapseEnableDelay::max))
	    : write_delay(wrdel),
	      output_delay(oedel),
	      setup_precharge(predel),
	      enable_delay(endel) {}

	SynapseWriteDelay write_delay;
	SynapseOutputDelay output_delay;
	SynapseSetupPrecharge setup_precharge;
	SynapseEnableDelay enable_delay;

	bool operator==(const SynapseControllerTimings& s) const {
		return (write_delay == s.write_delay) &&
			(output_delay == s.output_delay) &&
			(setup_precharge == s.setup_precharge) &&
			(enable_delay == s.enable_delay);
	}

private:
	friend class boost::serialization::access;
	template <typename Archiver>
	void serialize(Archiver& ar, const unsigned int) {
		using boost::serialization::make_nvp;
		ar & make_nvp("write_delay", write_delay)
		   & make_nvp("output_delay", output_delay)
		   & make_nvp("setup_precharge", setup_precharge)
		   & make_nvp("enable_delay", enable_delay);
	}

	friend std::ostream& operator<<(std::ostream& os, SynapseControllerTimings const& o);
};

struct RepeaterBlock
{
public:

	//test event type: neuron number and release time
	struct TestEvent {
		L1Address address; //neuron number
		uint16_t time;     //time
		PYPP_CONSTEXPR TestEvent() : address(0), time(0) {}
		PYPP_CONSTEXPR TestEvent(L1Address a, uint16_t t) : address(a), time(t) {}

		bool operator==(const TestEvent & e) const
		{	return address == e.address && time == e.time; }

		template<typename Archiver>
		void serialize(Archiver& ar, unsigned int const)
		{
			ar & boost::serialization::make_nvp("address", address)
			   & boost::serialization::make_nvp("time", time);
		}

		private:
			friend std::ostream& operator<<(std::ostream& os, TestEvent const& o);
	};

	PYPP_CONSTEXPR RepeaterBlock() :
		drvresetb(1),
		dllresetb(1),
		fextcap(0),
		start_tdi(0),
		start_tdo(0),
		full_flag(0),
		tdi_data(), //container to store test input packets
		tdo_data(), //container to store test output packets
		timings(SRAMReadDelay(64), SRAMSetupPrecharge(8), SRAMWriteDelay(8))
	{}

	bool                     drvresetb;
	bool                     dllresetb;
	std::bitset<2>           fextcap;
	std::bitset<HMF::Coordinate::TestPortOnRepeaterBlock::end> start_tdi;
	std::bitset<HMF::Coordinate::TestPortOnRepeaterBlock::end> start_tdo;
	std::bitset<HMF::Coordinate::TestPortOnRepeaterBlock::end> full_flag;
	//each block has a capacity of HMF::Coordinate::TestPortOnRepeaterBlock::end*3 incoming/outgoing events (output is looped)
	std::array<std::array<TestEvent, 3>, HMF::Coordinate::TestPortOnRepeaterBlock::end> tdi_data; // incoming data
	std::array<std::array<TestEvent, 3>, HMF::Coordinate::TestPortOnRepeaterBlock::end> tdo_data; // outgoing data
	SRAMControllerTimings timings;

	bool operator == ( const RepeaterBlock & o) const
	{ return drvresetb == o.drvresetb &&
			dllresetb == o.dllresetb &&
			fextcap == o.fextcap &&
			start_tdi == o.start_tdi &&
			start_tdo == o.start_tdo &&
			full_flag == o.full_flag &&
			tdi_data  == o.tdi_data &&
			tdo_data  == o.tdo_data &&
			timings == o.timings;
	}
private:
	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, unsigned int const version)
	{
		using boost::serialization::make_nvp;
		ar & make_nvp("fextcap", fextcap)
		   & make_nvp("drvresetb", drvresetb)
		   & make_nvp("dllresetb", dllresetb)
		   & make_nvp("start_tdi", start_tdi)
		   & make_nvp("start_tdo", start_tdo)
		   & make_nvp("full_flag", full_flag)
		   & make_nvp("tdi_data", tdi_data)
		   & make_nvp("tdo_data", tdo_data);
		if (version > 0) {
			ar& make_nvp("timings", timings);
		}
	}
	friend std::ostream& operator<< (std::ostream& os, RepeaterBlock const& o);
};

STRONG_TYPEDEF_CONSTEXPR(TestEvent_3,
                         std::array<RepeaterBlock::TestEvent BOOST_PP_COMMA() 3>,
                         PYPP_CONSTEXPR)

#define HMF_ANALOG_ACCESS(NAME) \
	bool get_ ## NAME (Coordinate::AnalogOnHICANN const side) const { \
		assert(config.count() <= 4); \
		return config[mult(side) * 10 + NAME]; \
	} \
	void set_ ## NAME (Coordinate::AnalogOnHICANN const side) { \
		for (size_t ii=0; ii<10; ++ii) { \
			config[mult(side) * 10 + ii] = false; \
		} \
		if (NAME != Analog::none) { \
			config[mult(side) * 10 + NAME] = true; \
		} \
		enable(side); \
	}



class Analog;
void set_analogIMPL(Handle::HICANNHw & h, Analog const& a);
Analog get_analogIMPL(Handle::HICANNHw & h);

class Analog
{
public:
	typedef size_t value_type;

	Analog() : config(0) {}

	bool operator ==(Analog const& rhs) const;

	static const value_type
		fg_right          = 0,
		membrane_bot_odd  = 2,
		membrane_top_odd  = 3,
		membrane_bot_even = 4,
		membrane_top_even = 5,
		fg_left           = 6,
		fireline_neuron0  = 7,
		preout            = 8,
		dll_voltage       = 9,
		none              = 10;

	bool enabled(Coordinate::AnalogOnHICANN const side) const
	{
		return config[20+mult(side)];
	}

	void enable (Coordinate::AnalogOnHICANN const side) { config[20+mult(side)] = true; }
	void disable(Coordinate::AnalogOnHICANN const side) { config[20+mult(side)] = false; }

	/// Setter an getter:
	/// set_fg_left(geometry::top) set left floating gate to geometry::top analog out and enable it
	HMF_ANALOG_ACCESS(dll_voltage)
	HMF_ANALOG_ACCESS(preout)
	HMF_ANALOG_ACCESS(fireline_neuron0) // TODO: in set_analog halbe function check which analog
	HMF_ANALOG_ACCESS(fg_left)
	HMF_ANALOG_ACCESS(membrane_top_even)
	HMF_ANALOG_ACCESS(membrane_bot_even)
	HMF_ANALOG_ACCESS(membrane_top_odd)
	HMF_ANALOG_ACCESS(membrane_bot_odd)
	HMF_ANALOG_ACCESS(fg_right)
	HMF_ANALOG_ACCESS(none)

	std::bitset<22> const& getConfig() const { return config; }
private:
	friend void set_analogIMPL(Handle::HICANNHw & h, Analog const& a);
	friend Analog HICANN::get_analogIMPL(Handle::HICANNHw & h);

	std::bitset<22>&       getConfig()       { return config; }
	size_t mult(Coordinate::AnalogOnHICANN const s) const;

	std::bitset<22> config;

	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, const unsigned int&)
	{
		ar & boost::serialization::make_nvp("config", config);
	}

	friend std::ostream& operator<< (std::ostream& os, Analog const& o);
};

///these classes contain more instance-specific info
///TODO: fill with useful information and functionality
class Status : public HMF::StatusBase
{
public:
	void check();
private:
	friend std::ostream& operator<< (std::ostream& os, Status const& o);
};


// FIXME: enhance STRONG_TYPEDEF to support non-array value types
class Phase : public std::bitset<8>
{
public:
	typedef std::bitset<8> base_t;
	Phase() : base_t() {};
	Phase(unsigned long val) : base_t(val) {};

private:
	friend class boost::serialization::access;
	template<typename Archiver>
	void serialize(Archiver& ar, const unsigned int&) {
		ar & boost::serialization::make_nvp("phase",
			         boost::serialization::base_object< std::bitset<8> >(*this)
		);

	}
};
typedef Phase phase_t __attribute__ ((deprecated));

} // namespace HICANN
} // namespace HMF

BOOST_CLASS_VERSION(::HMF::HICANN::NeuronConfig, 2)
BOOST_CLASS_VERSION(::HMF::HICANN::RepeaterBlock, 1)

namespace std {

HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::SynapseWeight)
HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::BkgRandomISI)
HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::BkgRegularISI)
HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::SRAMReadDelay)
HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::SRAMSetupPrecharge)
HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::SRAMWriteDelay)

HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::SynapseEnableDelay)
HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::SynapseSetupPrecharge)
HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::SynapseWriteDelay)
HALBE_GEOMETRY_HASH_CLASS(HMF::HICANN::SynapseOutputDelay)

} // namespace std

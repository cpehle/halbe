#include "FPGABackend.h"

#include <chrono>
#include <cstdint>
#include <sstream>

#include <boost/config.hpp>

#include "hal/Coordinate/FormatHelper.h"
#include "hal/Coordinate/iter_all.h"
#include "hal/backend/DNCBackend.h"
#include "hal/backend/FPGABackendHelper.h"
#include "hal/backend/HICANNBackendHelper.h"
#include "hal/backend/dispatch.h"
#include "sctrltp/ARQStream.h"

// TODO: ugly!
#include "hal/Handle/HMFRun.h"
#include "reticle_control.h"
#include "spinn_controller.h"
#include "RealtimeComm.h"
#include "jtag_cmdbase.h"

using namespace facets;
using namespace std;

static const uint64_t HICANN_JTAG_ID = 0x14849434;
static const uint64_t FPGA_JTAG_ID = 0x1c56c007;

namespace HMF {

namespace FPGA {

HALBE_SETTER_GUARDED(EventSystemStartup,
	reset,
	Handle::FPGA &, f)
{
	reset(f, Reset());
}


HALBE_SETTER_GUARDED(EventSystemStartup,
	reset,
	Handle::FPGA &, f,
	Reset const&, r)
{
	ReticleControl& reticle = f.getPowerBackend().get_some_reticle(f);
	auto jtag_p2fa = dynamic_cast<S2C_JtagPhys2FpgaArq*>(reticle.jtag_p2f.get());
	if (!jtag_p2fa)
		throw std::runtime_error("Only HICANN-ARQ comm model is currently supported.");

	// generate bool set indicating active hicanns
	std::bitset<8> hicanns;
	for (auto dnc : Coordinate::iter_all<HMF::Coordinate::DNCOnFPGA>())
		for (auto hicann : Coordinate::iter_all<HMF::Coordinate::HICANNOnDNC>())
			if (f.hicann_active(dnc, hicann))
				hicanns[f.getPowerBackend().hicann_reticle_addr(f.get(dnc, hicann)->coordinate())] = true;

	if (r.PLL_frequency != ((r.PLL_frequency / 25) * 25))
		throw std::runtime_error("only 50, 75, 100, 125, 150, 175, 200, 225, 250Mhz supported");
	uint8_t PLL_multiplier = r.PLL_frequency / 25;
	uint8_t PLL_divisior = 2;
	if (PLL_multiplier % 2 == 0) {
		PLL_multiplier /= 2;
		PLL_divisior /= 2;
	}

	auto ip = reticle.get_fpga_ip();
	uint32_t rawip = ip[0] << 24 | ip[1] << 16 | ip[2] << 8 | ip[3];

	// The "canonical" Reset Procedure as defined in HWFUM 2016-05-30
	// cf. https://brainscales-r.kip.uni-heidelberg.de/projects/symap2ic/wiki/HWFUMMeetingLog_20160530

	int max_init_trials = 10;
	bool highspeed_init_successful = false;
	while (!highspeed_init_successful) {

		{ // Reset FPGA and DNC
			if (!(r.core && r.fpgadnc && r.ddr2onboard && r.ddr2sodimm && r.arq))
				throw std::runtime_error("partial reset is not supported");

			// toggle on
			reticle.jtag_p2f->set_fpga_reset(
			    rawip, true /*r.core*/, // FPGA core
			    true /*r.fpgadnc*/,     // DNC & HICANN design reset (on Kintex and VSetups)
			    true /*r.ddr2onboard*/, // RAM controller...
			    true /*r.ddr2sodimm*/,  // RAM controller...
			    true /*r.arq*/);        // FPGA's HICANN-ARQ
			// toggle off
			reticle.jtag_p2f->set_fpga_reset(rawip, false, false, false, false, false);
		}

		{ // Reset HostARQ-based connection FPGA
			// FIXME: Has to be moved up as soon as we have JTAG-via-HostARQ...
			jtag_p2fa->getHostAL()->reset();
			jtag_p2fa->initHostAL();
		}

		{ // JTAG reset to enable JTAG-based communication with the HICANN
			reticle.jtag->reset_jtag();
		}

		{ // Read and verify JTAG IDs
			for (auto dnc : Coordinate::iter_all<HMF::Coordinate::DNCOnFPGA>()) {
				for (auto hicann : Coordinate::iter_all<HMF::Coordinate::HICANNOnDNC>()) {
					if (f.hicann_active(dnc, hicann)) {
						auto jtag_pos_hicann =
						    f.getPowerBackend().hicann_jtag_addr(f.get(dnc, hicann)->coordinate());
						auto const read_hicann_id = reticle.jtag->read_id(jtag_pos_hicann);
						if (read_hicann_id != HICANN_JTAG_ID) {
							std::stringstream error_msg;
							error_msg
							    << HMF::Coordinate::short_format(f.coordinate())
							    << "::reset: Read wrong (0x" << std::hex << read_hicann_id
							    << " instead of 0x" << HICANN_JTAG_ID << ") JTAG ID for "
							    << HMF::Coordinate::short_format(hicann.toHICANNOnWafer(
							           dnc.toDNCOnWafer(f.coordinate())))
							    << " after JTAG reset";
							throw std::runtime_error(error_msg.str().c_str());
						}
					}
				}
				if (f.isKintex())
					break; // there's no other "DNC" on Kintex-7
			}
			auto const read_fpga_id = reticle.jtag->read_id(reticle.jtag->pos_fpga);
			if (read_fpga_id != FPGA_JTAG_ID) {
				std::stringstream error_msg;
				error_msg << HMF::Coordinate::short_format(f.coordinate())
						  << "::reset: Read wrong (0x" << std::hex << read_fpga_id
						  << " instead of 0x" << FPGA_JTAG_ID
						  << ") JTAG ID after JTAG reset";
				throw std::runtime_error(error_msg.str().c_str());
			}
		}

		{ // Enable HICANN/chip design reset
			if (!f.isKintex())
				throw std::runtime_error("FPGA::reset: This implementation does not support old, "
				                         "Virtex-5 based setups...");
			// HICANNs (and DNCs) are resetted with r.fpgadnc (on VSetup and Kintex)
			// Keep FPGA's HICANN-ARQ off... (until end of high-speed init...)
			reticle.jtag_p2f->set_fpga_reset(
			    rawip,
			    false, // core
			    true,  // fpgadnc
			    false, // ddr2onboard
			    false, // ddr2sodimm
			    true); // arq
		}

		{ // Set PLLs of all chips... (design reset is held!)
			for (auto d : Coordinate::iter_all<HMF::Coordinate::DNCOnFPGA>()) {
				for (auto h : Coordinate::iter_all<HMF::Coordinate::HICANNOnDNC>()) {
					if (f.hicann_active(d, h)) {
						HICANN::set_PLL_multiplier(*f.get(d, h), PLL_divisior, PLL_multiplier);
					}
				}
				if (f.isKintex())
					break; // there's no other "DNC" on Kintex-7
			}
		}

		{ // Disable HICANN/chip design reset
			if (!f.isKintex())
				throw std::runtime_error("FPGA::reset: This implementation does not support old, "
				                         "Virtex-5-based setups...");
			// FPGA's HICANN-ARQ still in reset (until end of high-speed init...)
			reticle.jtag_p2f->set_fpga_reset(
			    rawip,
			    false, // core
			    false, // fpgadnc
			    false, // ddr2onboard
			    false, // ddr2sodimm
			    true); // arq
		}

		if (r.enable_tests)
		{ // Verify JTAG-based communication link and some digital functionality
			LOG4CXX_INFO(logger, HMF::Coordinate::short_format(f.coordinate())
			                         << " Verifying jtag based communication link");
			Helper::switchramtest_jtag(f, reticle);
		}

		{ // Reset HICANN ARQ again... (switchram test spoilt it)
			LOG4CXX_INFO(logger, HMF::Coordinate::short_format(f.coordinate())
			                         << " Resetting HICANN ARQ");
			for (auto d : Coordinate::iter_all<HMF::Coordinate::DNCOnFPGA>()) {
				for (auto h : Coordinate::iter_all<HMF::Coordinate::HICANNOnDNC>()) {
					if (f.hicann_active(d, h)) {
						reticle.jtag->set_hicann_pos(f.get(d, h)->jtag_addr());
						// reset HICANN's ARQ (both tags)
						reticle.jtag->HICANN_arq_write_ctrl(
							jtag_cmdbase::CMD3_ARQ_CTRL_RESET, jtag_cmdbase::CMD3_ARQ_CTRL_RESET);
						reticle.jtag->HICANN_arq_write_ctrl(0x00, 0x00);
						reticle.jtag->HICANN_set_reset(1); // re-enable DNC interface... (was disabled
														   // by JTAG-based comm class Init)
					}
				}
				if (f.isKintex())
					break; // there's no other "DNC" on Kintex-7
			}
		}

		{ // Initialize High-Speed Connection
			auto tmp = jtag_p2fa->trans_count;
			jtag_p2fa->trans_count = r.cnt_hicann_init_tests;
			for (auto dnc : Coordinate::iter_all<HMF::Coordinate::DNCOnFPGA>()) {
				if (hicanns.any()) {
					highspeed_init_successful = f.get_reticle(dnc)->hicannInit(
					    hicanns, /*silent*/ false, /*return_on_error*/ true);
				}
				if (max_init_trials-- <= 0) {
					std::stringstream error_msg;
					error_msg
					    << HMF::Coordinate::short_format(f.coordinate())
					    << "::reset: Initalization of high speed links failed. Stop";
					throw std::runtime_error(error_msg.str().c_str());
				}
				if (f.isKintex())
					break; // there's no other "DNC" on Kintex-7
			}
			jtag_p2fa->trans_count = tmp;
		}
	}

	if (r.enable_tests)
	{ // Verify HICANN-ARQ-based communication link and some digital functionality
		Helper::switchramtest_arq(f, reticle);
	}
}

HALBE_SETTER_GUARDED(EventStartExperiment,
	init,
	Handle::FPGA &, f,
	bool const, zero_synapses)
{
	ReticleControl& reticle = f.getPowerBackend().get_some_reticle(f);

	for (auto d : Coordinate::iter_all<HMF::Coordinate::DNCOnFPGA>()) {
		for (auto h : Coordinate::iter_all<HMF::Coordinate::HICANNOnDNC>()) {
			if (f.hicann_active(d, h)) {
				HicannCtrl& hc = *reticle.hicann[f.get(d, h)->jtag_addr()];
				DNCControl& dc = *reticle.dc;
				LOG4CXX_INFO(logger,
				             HMF::Coordinate::short_format(f.coordinate())
				                 << " init: "
				                 << h.toHICANNOnWafer(d.toDNCOnWafer(f.coordinate())));
				HMF::HICANN::hicann_init(hc, dc, f.isKintex(), zero_synapses);
			}
		}
		if (f.isKintex())
			break; // there's no other "DNC" on Kintex-7
	}
}

HALBE_SETTER(reset_pbmem, Handle::FPGA &, f)
{
	HostALController& host_al = f.getPowerBackend().get_host_al(f);
	host_al.reset_fpga_times();
}

HALBE_GETTER(Status, get_fpga_status,
	Handle::FPGA &, f)
{
	ReticleControl& reticle = f.getPowerBackend().get_some_reticle(f);

	Status returnvalue = Status();

	uint64_t id = 0;
	reticle.jtag->read_id(id, reticle.jtag->pos_fpga);
	returnvalue.setHardwareId(id);
	returnvalue.set_git_hash(reticle.fc->get_bitfile_git_hash());
	returnvalue.set_git_dirty_flag(reticle.fc->get_git_dirty_flag());
	returnvalue.set_trace_pulse_count(reticle.fc->get_trace_pulse_count());
	returnvalue.set_pb_pulse_count(reticle.fc->get_pb_pulse_count());
	returnvalue.set_hicann_arq_downlink_rx_counter(reticle.fc->get_network_debug_reg_rx());
	returnvalue.set_hicann_arq_downlink_tx_counter(reticle.fc->get_network_debug_reg_tx());
	returnvalue.set_hicann_arq_uplink_rx_counter(reticle.fc->get_ul_packet_cnt_r());
	returnvalue.set_hicann_arq_uplink_tx_counter(reticle.fc->get_ul_packet_cnt_w());
	returnvalue.set_pb_release_error(reticle.fc->get_pb_release_error());
	returnvalue.set_pb2arq_fifo_overflow(reticle.fc->get_pb2arq_fifo_overflow());

	return returnvalue;
}

HALBE_SETTER(
	prime_systime_counter,
	Handle::FPGA &, f)
{
	ReticleControl& reticle = f.getPowerBackend().get_some_reticle(f);
	auto jtag_p2fa = dynamic_cast<S2C_JtagPhys2FpgaArq*>(reticle.jtag_p2f.get());
	if (!jtag_p2fa)
		throw std::runtime_error("Only HICANN-ARQ comm model is currently supported.");
	unsigned int curr_ip = reticle.jtag->get_ip();

	// Prime systime counter on FPGA. When trigger_systime systimes second
	// parameter is false, FPGA gets "primed". Don't get confused by non
	// perfect naming of functions
	LOG4CXX_DEBUG(logger, __func__ << " "
	                               << HMF::Coordinate::short_format(f.coordinate()));
	jtag_p2fa->trigger_systime(curr_ip, false, f.getListenGlobalMode());

	// reset all time counters on HICANNs
	for (auto dnc : Coordinate::iter_all<HMF::Coordinate::DNCOnFPGA>() ) {
		if (f.dnc_active(dnc)) {
			LOG4CXX_DEBUG(logger,
			              __func__ << " " << HMF::Coordinate::short_format(f.coordinate())
			                       << " reset all time counters");
			f.get_reticle(dnc)->jtag->HICANNv2_reset_all_time_counters();
		}
	}
}

HALBE_SETTER(
	disable_global,
	Handle::FPGA &, f)
{

	ReticleControl& reticle = f.getPowerBackend().get_some_reticle(f);
	auto jtag_p2fa = dynamic_cast<S2C_JtagPhys2FpgaArq*>(reticle.jtag_p2f.get());
	if (!jtag_p2fa)
		throw std::runtime_error("Only HICANN-ARQ comm model is currently supported.");
	unsigned int curr_ip = reticle.jtag->get_ip();

	LOG4CXX_DEBUG(logger, __func__ << " " << f.coordinate());
	jtag_p2fa->trigger_experiment(curr_ip, false, false);
}

HALBE_SETTER(
	start_systime_counter,
	Handle::FPGA &, f)
{
	ReticleControl& reticle = f.getPowerBackend().get_some_reticle(f);
	auto jtag_p2fa = dynamic_cast<S2C_JtagPhys2FpgaArq*>(reticle.jtag_p2f.get());
	if (!jtag_p2fa)
		throw std::runtime_error("Only HICANN-ARQ comm model is currently supported.");
	unsigned int curr_ip = reticle.jtag->get_ip();

	// Start systime counter either in local listen mode or if FPGA is Master FPGA
	if (!f.getListenGlobalMode() || (f.getListenGlobalMode() && f.isMaster())) {
		LOG4CXX_DEBUG(logger, __func__ << " " << f.coordinate());
		jtag_p2fa->trigger_systime(curr_ip, true, f.getListenGlobalMode());
	}
}

HALBE_SETTER(
	prime_experiment,
	Handle::FPGA&, f)
{
	ReticleControl& reticle = f.getPowerBackend().get_some_reticle(f);
	auto jtag_p2fa = dynamic_cast<S2C_JtagPhys2FpgaArq*>(reticle.jtag_p2f.get());
	if (!jtag_p2fa)
		throw std::runtime_error("Only HICANN-ARQ comm model is currently supported.");
	unsigned int curr_ip = reticle.jtag->get_ip();

	// prime FPGA for Experiment;
	LOG4CXX_DEBUG(logger, __func__ << " " << f.coordinate());
	jtag_p2fa->trigger_experiment(curr_ip, false, f.getListenGlobalMode());
}

HALBE_SETTER(
	start_experiment,
	Handle::FPGA &, f)
{
	ReticleControl& reticle = f.getPowerBackend().get_some_reticle(f);
	auto jtag_p2fa = dynamic_cast<S2C_JtagPhys2FpgaArq*>(reticle.jtag_p2f.get());
	if (!jtag_p2fa)
		throw std::runtime_error("Only HICANN-ARQ comm model is currently supported.");
	unsigned int curr_ip = reticle.jtag->get_ip();

	//TODO add fpga pulse loopback functionality to api
	//host_al.setFPGAPulseLoopback(playback_loop);

	// Start experiment either in local listen mode or if FPGA is Master FPGA
	if (!f.getListenGlobalMode() || (f.getListenGlobalMode() && f.isMaster())) {
		LOG4CXX_DEBUG(logger, __func__ << " " << f.coordinate());
		jtag_p2fa->trigger_experiment(curr_ip, true, f.getListenGlobalMode());
	}
}

HALBE_SETTER_GUARDED(EventSetupL2,
	set_fpga_background_generator,
	Handle::FPGA &, f,
	Coordinate::DNCOnFPGA const, d,
	BackgroundGenerator const&, bg)
{
	ReticleControl& reticle = f.getPowerBackend().get_reticle(f, d);

	// transforming hicann coordinate to a number that FPGA BEG would accept
	uint8_t hc = (bg.hicann_number.y()) ? (bg.hicann_number.x())*2 + 1 /*odd*/ : (bg.hicann_number.x())*2 /*even*/;
	cout << "Set hicann to " << (uint)hc << endl;

	reticle.jtag->FPGA_set_cont_pulse_ctrl(bg.enable, bg.get_hw_channels(), bg.poisson,
		bg.rate, bg.seed, bg.first_address, bg.last_address, hc);
}

// TODO: uint16_t is ugly!
HALBE_SETTER_GUARDED(EventSetupL2,
	write_playback_pulses,
	Handle::FPGA &, f,
	PulseEventContainer const&, st,
	PulseEvent::spiketime_t, runtime,
	uint16_t,fpga_hicann_delay)
{
	HostALController& host_al = f.getPowerBackend().get_host_al(f);

	size_t const npulses = st.size();
	uint64_t last_fpga_time = 0;
	for (size_t n = 0; n < npulses; ++n) {
		PulseEvent pe = st[n];
		if (pe.getTime() < fpga_hicann_delay*2)
			throw std::runtime_error(
				"write_playback_pulses: the time of the PulseEvent in the spike "
				"list has to be greater or equal than fpga_hicann_delay*2");
		last_fpga_time = pe.getTime()/2 - fpga_hicann_delay;
		uint16_t id =  pe.getLabel();
		host_al.addPlaybackPulse(last_fpga_time, /*uint16_t hicann_time*/ pe.getTime(), id);
	}

	// Calculate EoE timestamp in FPGA clock cycles, devide by two as DNC frequency == 2 * FPGA frequency
	size_t end_of_experiment_timestamp = (runtime + 1) / 2;
	if (end_of_experiment_timestamp < last_fpga_time)
		throw std::runtime_error("write_playback_pulses: runtime shorter than spike trains length");

	// Add end of experiment marker
	host_al.addPlaybackFPGAConfig(
	    end_of_experiment_timestamp, true /*end_mark*/, true /*stop trace*/, true /*start trace read*/);
	bool success = host_al.flushPlaybackPulses();
	if (!success)
		throw std::runtime_error("write_playback_pulses: failed to send pulse packets to FPGA");
}

// FIXME: Adapt scheriff to upcoming canonical state machine from spec
HALBE_GETTER(bool, get_pbmem_buffering_completed,
	Handle::FPGA &, f
	)
{
	HostALController& host_al = f.getPowerBackend().get_host_al(f);
	return (host_al.getPlaybackEndAddress() != 0);
}

HALBE_GETTER(AlmostSortedPulseEvents, read_trace_pulses,
	Handle::FPGA &, f,
	PulseEvent::spiketime_t const, runtime,
	bool const, drop_background_events
	)
{
	// We have to handle all possible types of packets described in section
	// "I-10.2.1. FPGA Trace / Pulse Data" of the specification, i.e. pulse
	// entries and overflow indicators.

	// Each 64 bit packet consists of two entries aligned to 32 bit.

	constexpr size_t EVENT_TIMESTAMP_BITS = 15;
	union entry_type
	{
		std::uint32_t raw;
		// A pulse event is indicated by two '0' high-order bits.  The sequence '01'
		// is used to mark the absence of a pulse event in a packet that contains an
		// overflow indicator.
		struct
		{
			// LSBs of pulse event timestamp, MSBs are transmitted via overflow indicators.
			unsigned int timestamp : EVENT_TIMESTAMP_BITS;
			// Label of pulse event, as described below.
			unsigned int label : 12;
			unsigned int : 2; // padding
			// MSB of the FPGA systime counter.
			unsigned int fpga_msb : 1;
			// Two zero-bits, to encode entry type.
			unsigned int zero_bits : 2;
		} event;
		// An overflow indicator is indicated by a '1' high-order bit.
		struct
		{
			unsigned int count : 31;
			unsigned int is_overflow : 1;
		} overflow;
	};

	// A pulse event label consists of 12 bit used as follows:
	// |     3bit    |   3bit   |    6bit   |
	// | HICANNOnDNC | GbitLink | L1Address |
	// Each pulse label will be converted to a 16 bit PulseAddress,
	// where the additional 4 bit remain unused.

	// Count the number of overflow indicators to calculate MSBs of full time stamp.
	std::uint64_t trace_overflow_count = 0;
	constexpr std::uint64_t MAX_TIMESTAMP_CNT = 1 << EVENT_TIMESTAMP_BITS;

	bool const trace_enabled = logger->isTraceEnabled();
#define HALBE_RTP_TRACE(message) \
	{ \
		if (LOG4CXX_UNLIKELY(trace_enabled)) { \
			::log4cxx::helpers::MessageBuffer oss_; \
			logger->forcedLog( \
			    ::log4cxx::Level::getTrace(), oss_.str(oss_ << message), LOG4CXX_LOCATION); \
		} \
	}

	size_t dropped_events = 0;
	AlmostSortedPulseEvents::container_type pulse_events;
	auto const receive_pulse_events =
		[trace_enabled, drop_background_events, &dropped_events, &pulse_events,
		 &trace_overflow_count,
		 &f](sctrltp::ARQStream* const arq_ptr) -> std::tuple<bool, std::uint64_t> {

		bool received_eot = false;
		std::uint64_t received_pulse_events_count = 0;
		// FIXME@ECM: defined in hicann-system/…/ARQFrame.h (no namespace)
		sctrltp::packet current_packet;
		while ((!received_eot) && arq_ptr->receive(current_packet)) {
			HALBE_RTP_TRACE("received hostARQ packet with " << current_packet.len << " entries");
			if (BOOST_UNLIKELY(current_packet.pid !=
			                   application_layer_packet_types::FPGATRACE)) {
				LOG4CXX_ERROR(logger,
				              HMF::Coordinate::short_format(f.coordinate())
				                  << " unexpected frame type in read_trace_pulses: "
				                  << current_packet.pid);
				throw std::runtime_error("unexpected frame type in read_trace_pulses");
			}

			entry_type* const entries = reinterpret_cast<entry_type*>(current_packet.pdu);

			for (std::uint16_t ii = 0; ii < 2 * current_packet.len; ++ii) {

				// check for end-of-trace marker every 64-bit word
				if ((ii % 2) == 0) {
					if (current_packet.pdu[ii/2] == 0x4000E11D40000000ull) {
						if (((ii/2) + 1) < current_packet.len) {
							std::stringstream debug_msg;
							debug_msg << HMF::Coordinate::short_format(f.coordinate())
							          << " unexpected end-of-trace marker"
							             " within other data: " << ii / 2 << " out of "
							          << (current_packet.len - 1) << ".\n"
							          << " Next entry looks like: " << std::hex
							          << current_packet.pdu[(ii / 2) + 1] << std::dec
							          << "\n";
							LOG4CXX_ERROR(logger, debug_msg.str());
							// FIXME: we should throw std::runtime_error(debug_msg.str()); here
						}
						// packet handling done, bail out
						received_eot = true;
						break;
					}
				}

				// non-eot data handling below
				auto const& entry = entries[ii];

				if (entry.overflow.is_overflow) {
					if (BOOST_UNLIKELY(ii % 2 != 1)) {
#ifndef NDEBUG
						// Overflow entries should only occur at odd indices.
						LOG4CXX_WARN(logger,
						             HMF::Coordinate::short_format(f.coordinate())
						                 << " garbage overflow entry at even index " << ii
						                 << ": " << std::showbase << std::hex << entry.raw
						                 << " (issue 2355)");
#endif // !NDEBUG
						continue;
					}
					trace_overflow_count += 1;

#ifndef NDEBUG
					HALBE_RTP_TRACE(
						"overflow packet " << trace_overflow_count
						<< std::showbase
						<< " with value " << std::hex << entry.overflow.count
						<< " (" << std::dec << entry.overflow.count << ")" << " received.\n"
						<< " current offset is " << std::hex << trace_overflow_count * MAX_TIMESTAMP_CNT
						<< " (" << std::dec << trace_overflow_count * MAX_TIMESTAMP_CNT << ")");

					if (trace_overflow_count != entry.overflow.count) {
						LOG4CXX_WARN(
						    logger,
						    HMF::Coordinate::short_format(f.coordinate())
						        << " Local overflow count " << trace_overflow_count
						        << " does not match contents of overflow indicator "
						        << entry.overflow.count);
					}
#endif // !NDEBUG

					continue;
				} else if (entry.event.zero_bits != 0) {
					// no overflow, no spike => garbage
					continue;
				}

				std::uint64_t full_timestamp =
					static_cast<std::uint64_t>(entry.event.timestamp) +
					trace_overflow_count * MAX_TIMESTAMP_CNT;
				bool timestamp_msb = entry.event.timestamp >> (EVENT_TIMESTAMP_BITS - 1);

				// Detect special case that HICANN timestamp was registered before
				// overflow, but pulse arrives in FPGA after overflow and an overflow
				// packet was generated.
				if (timestamp_msb && !entry.event.fpga_msb) {
					if (full_timestamp < MAX_TIMESTAMP_CNT) {
						// Ignore early pulses.
						continue;
					}
					// Undo last overflow for that pulse.
					full_timestamp -= MAX_TIMESTAMP_CNT;
				}

#ifndef NDEBUG
				HALBE_RTP_TRACE(
					"received pulse event " << received_pulse_events_count << " (entry " << ii << "):\n"
					<< std::showbase
					<< "id: " << std::hex << entry.event.label << ", "
					<< "timestamp: " << std::hex << entry.event.timestamp
					<< " (" << std::dec << entry.event.timestamp << ")" << ", "
					<< "msb timestamp/fpga: " << timestamp_msb << "/" << entry.event.fpga_msb << ",\n"
					<< "full timestamp: " << std::hex << full_timestamp
					<< " (" << std::dec << full_timestamp << ")");

				if (!pulse_events.empty()) {
					auto const& last_event = pulse_events.back();

					// Old bug where trace memory potentially stored pulse twice while
					// sending overflow packet, should not occur anymore.
					// TODO 2016-04-27: Remove check when it's absolutely sure that the bug is
					// fixed.
					if (last_event.getLabel() == entry.event.label &&
					    (last_event.getTime() == full_timestamp ||
					     (last_event.getTime() + MAX_TIMESTAMP_CNT) == full_timestamp)) {
						LOG4CXX_WARN(logger,
						             HMF::Coordinate::short_format(f.coordinate())
						                 << " received pulse twice (issue 2022): "
						                 << pulse_events.back().getTime()
						                 << " == " << full_timestamp << "\n(32 bit entry "
						                 << ii << "/" << (2 * current_packet.len)
						                 << " of ARQ frame, spike " << pulse_events.size()
						                 << ") with label " << entry.event.label);
					}
				}
#endif // !NDEBUG

				// Update the count of received pulse events unconditionally, as it is
				// used to decide when to timeout below.
				++received_pulse_events_count;

				if (drop_background_events && !(entry.event.label & HICANN::L1Address::max)) {
					++dropped_events;
					continue;
				}

				pulse_events.push_back(
					PulseEvent(PulseAddress(entry.event.label), full_timestamp));
			}
		}
		return std::make_tuple(received_eot, received_pulse_events_count);
	}; // receive_pulse_events

#undef HALBE_RTP_TRACE

	if (drop_background_events) {
		LOG4CXX_INFO(logger, HMF::Coordinate::short_format(f.coordinate())
		                         << " background pulse events will be dropped");
	}

	HostALController& host_al = f.getPowerBackend().get_host_al(f);
	unsigned int sleep_duration_in_us = 500;

	sctrltp::ARQStream* const arq_ptr = host_al.getARQStream();

	/* We read(receive) data until we see the end-of-trace marker packet.
	 * However, as the connection might die at any time ("cable being pulled", whatever)
	 * we cannot just block here but rather have a relaxed timeout as it will only
	 * trigger in error cases.
	 * Details:
	 * The first timeout should be related to the experiment runtime as experiments
	 * can run for macroscopic time intervals => we add the default_timeout to the
	 * experiment runtime for the first timeout. As soon as we receive the first bunch
	 * of data, we can set the timeout to default_timeout and wait for the next data
	 * packet which will reset the timeout to default_timeout again (and again...).
	 * Finally, we receive the end-of-trace marker and stop waiting for new data.
	 * In case of an error (FPGA dead, cable pulled) we could enter up in a state
	 * where no end-of-trace packet is ever received. To avoid a deadlock in software,
	 * (please note that the HostARQ timeout would not trigger in this case, as the
	 * FPGA is mastering this data transfer and we don't know if data is missing)
	 * we apply a timeout which will trigger in these case and throw an exception.
	 */
	static constexpr std::chrono::milliseconds default_timeout{10000};

	// set initial timeout
	std::chrono::milliseconds const initial_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(default_timeout +
		std::chrono::microseconds{runtime / DNC_frequency_in_MHz});
	std::chrono::milliseconds timeout = initial_timeout;

	auto time_of_last_packet = std::chrono::steady_clock::now();
	auto now = time_of_last_packet;
	bool received_eot = false;
	while (!received_eot) {
		now = std::chrono::steady_clock::now();
		if ((now - time_of_last_packet) > timeout) {
			std::stringstream debug_msg;
			debug_msg << HMF::Coordinate::short_format(f.coordinate())
			          << ": No end-of-trace marker received in "
			          << std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count() << "ms.";
			LOG4CXX_ERROR(logger, debug_msg.str());
			throw std::runtime_error(debug_msg.str());
		}

		std::uint64_t num_pulses;
		std::tie(received_eot, num_pulses) = receive_pulse_events(arq_ptr);
		if (num_pulses > 0) {
			// initial timeout done, now set to default timeout
			timeout  = default_timeout;

			LOG4CXX_TRACE(logger,
			              HMF::Coordinate::short_format(f.coordinate())
			                  << " received " << num_pulses
			                  << " pulse events after waiting for "
			                  << std::chrono::duration_cast<std::chrono::milliseconds>(
			                         now - time_of_last_packet).count() << " ms");
			time_of_last_packet = now;
			continue;
		}

		if ((usleep(sleep_duration_in_us) != 0) && (errno != EINTR)) {
			throw std::runtime_error("usleep failed in read_trace_pulses");
		}
	}
	LOG4CXX_INFO(logger, HMF::Coordinate::short_format(f.coordinate())
	                         << " received " << (dropped_events + pulse_events.size())
	                         << " pulse events");

	return AlmostSortedPulseEvents(std::move(pulse_events), dropped_events);
}


HALBE_SETTER_GUARDED(EventStartExperiment,
	set_spinnaker_receive_port,
	Handle::FPGA &, f,
    uint16_t, port) {
	SpinnController & sc = f.get_spinn_controller();
	if( !sc.writePulsePort(port) )
		throw std::runtime_error("Spinnaker controller failed somehow");
}


HALBE_SETTER_GUARDED(EventSetupL2,
	set_spinnaker_routing_table,
     Handle::FPGA &, f,
     SpinnRoutingTable const&, spinn_routing_table
     ) {
	SpinnController & sc = f.get_spinn_controller();
	// sc.clearRoutingEntries();
	for (size_t i = 0; i<SpinnRoutingTable::num_entries; ++i)
		sc.setRoutingEntry(i, spinn_routing_table.get(SpinnInputAddress_t(i)).getLabel());
	if (!sc.writeRoutingMemory())
		throw std::runtime_error("Spinnaker controller failed somehow");
}


HALBE_SETTER_GUARDED(EventSetupL2,
	set_spinnaker_pulse_upsampler,
	Handle::FPGA &, f,
	size_t, upsample_count) {
	SpinnController & sc = f.get_spinn_controller();
	if (!sc.writePulseMultiplier(upsample_count))
		throw std::runtime_error("Spinnaker controller failed somehow");
}


HALBE_SETTER_GUARDED(EventSetupL2,
	set_spinnaker_pulse_downsampler,
	Handle::FPGA &, f,
	size_t, downsample_count) {
	SpinnController & sc = f.get_spinn_controller();
	if (!sc.writeDownSampleCount(downsample_count))
		throw std::runtime_error("Spinnaker controller failed somehow");
}


HALBE_SETTER_GUARDED(EventSetupL2,
	add_spinnaker_pulse,
	Handle::FPGA &, f,
	SpinnInputAddress_t const&, spinn_address) {
	SpinnController & sc = f.get_spinn_controller();
	sc.addPulse(spinn_address);
}


HALBE_SETTER_GUARDED(EventSetupL2,
	send_spinnaker_pulses,
	Handle::FPGA &, f) {
	SpinnController & sc = f.get_spinn_controller();
	sc.sendPulses();
}


HALBE_GETTER(SpinnOutputAddress_t, get_received_spinnaker_pulse,
	Handle::FPGA &, f) {
	SpinnController & sc = f.get_spinn_controller();
	int received_spinn_address = sc.getReceivedPulse();
	if (received_spinn_address == -1)
		throw std::runtime_error("Spinnaker controller did not receive any pulse");
	return SpinnOutputAddress_t(received_spinn_address);
}


HALBE_SETTER_GUARDED(EventSetupL2,
	set_spinnaker_sender_config,
	Handle::FPGA &, f,
	SpinnSenderConfig const&, cfg) {
	SpinnController & sc = f.get_spinn_controller();
	if (!sc.writeSenderSettings(cfg.getActive(), cfg.getTargetIP().to_ulong(), cfg.getTargetPort()))
		throw std::runtime_error("Spinnaker controller failed somehow");
}


HALBE_SETTER_GUARDED(EventSetupL2,
	set_spinnaker_address_config,
	Handle::FPGA &, f,
	SpinnAddressConfig const&, cfg
) {
	SpinnController & sc = f.get_spinn_controller();
	if (!sc.writeOutputAddressConfiguration(
				cfg.getSingleHicannMode(),
				cfg.getSingleHicannAddress(),
				cfg.getAddressOffset(),
				cfg.getInAddressMode(),
				cfg.getOutAddressMode())
	)
		throw std::runtime_error("Spinnaker controller failed somehow");
}


HALBE_SETTER_GUARDED(EventStartExperiment,
	send_realtime_pulse,
	Handle::FPGA &, f,
	SpinnInputAddress_t, spinn_address
) {
	send_spinnaker_realtime_pulse(f, Realtime::spike_h({spinn_address}));
}


HALBE_SETTER_GUARDED(EventStartExperiment,
	queue_spinnaker_realtime_pulse,
	Handle::FPGA &, f,
	Realtime::spike_h, s
) {
	auto& rc = f.get_realtime_comm();
	s.hton();
	rc.queue_spike<Realtime::spike_h>(std::move(s));
}


HALBE_SETTER_GUARDED(EventStartExperiment,
	send_spinnaker_realtime_pulse,
	Handle::FPGA &, f,
	Realtime::spike_h, s
) {
	auto& rc = f.get_realtime_comm();
	s.hton();
	rc.send_single_spike<Realtime::spike_h>(std::move(s));
}


HALBE_SETTER_GUARDED(EventStartExperiment,
	send_custom_realtime_pulse,
	Handle::FPGA &, f,
	Realtime::spike, s
) {
	auto& rc = f.get_realtime_comm();
	// sp.hton(); // FIXME
	rc.send_single_spike<Realtime::spike>(std::move(s));
}

HALBE_GETTER(std::vector<SpinnOutputAddress_t>, get_received_realtime_pulses,
	Handle::FPGA &, f
) {
	auto& rc = f.get_realtime_comm();
	std::vector<SpinnOutputAddress_t> ret;
	for (auto sp : rc.receive<Realtime::spike_h>()) {
		sp->ntoh();
		ret.push_back(static_cast<SpinnOutputAddress_t>(sp->label));
	}
	rc.free_receive();
	return ret;
}

HALBE_GETTER(SpinnOutputAddress_t, spin_and_get_next_realtime_pulse,
	Handle::FPGA &, f
) {
	auto sp = spin_and_get_next_realtime_pulse_as_spinnaker(f);
	return SpinnOutputAddress_t(sp.label);
}

    
HALBE_GETTER(Realtime::spike, spin_and_get_next_realtime_pulse_as_custom,
	Handle::FPGA &, f
) {
	auto& rc = f.get_realtime_comm();
	auto sp = rc.receive_and_spin<Realtime::spike>();
	Realtime::spike ret(*sp);
	// ret.ntoh(); // FIXME
	rc.free_receive();
	return ret;
}

HALBE_GETTER(Realtime::spike_h, spin_and_get_next_realtime_pulse_as_spinnaker,
	Handle::FPGA &, f
) {
	auto& rc = f.get_realtime_comm();
	auto sp = rc.receive_and_spin<Realtime::spike_h>();
	Realtime::spike_h ret(*sp);
	ret.ntoh();
	rc.free_receive();
	return ret;
}

} //namespace FPGA

} //namespace HMF

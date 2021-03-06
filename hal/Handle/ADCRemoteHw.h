#pragma once

#include <boost/serialization/nvp.hpp>
#include <boost/serialization/serialization.hpp>

#include "hal/Handle/ADC.h"


// fwd decls
struct I_HALbeADC;

template <typename T>
struct RcfClient;

template <>
struct RcfClient<I_HALbeADC>;

namespace RCF {
struct RcfInitDeinit;
}


namespace HMF {

namespace Handle {

/**
 * @class ADCRemoteHw
 *
 * @brief Handle class that encapsulates a single remote (via RCF) ADC connection.
 */
struct ADCRemoteHw : public ADC {
	/// Open connection to specific ADC (identified by its serial number == Coordinate)
	explicit ADCRemoteHw(Coordinate::IPv4 const& host, Coordinate::TCPPort const& port,
	                     HMF::ADC::USBSerial const& adc);

	/// Used for Serialization/Deserialization
	ADCRemoteHw();

	/// Close connection to ADC board
	~ADCRemoteHw();

	HMF::ADC::USBSerial boardId() const;

	Coordinate::IPv4 host() const;

	Coordinate::TCPPort port() const;

#ifndef PYPLUSPLUS
private:
	std::unique_ptr<RCF::RcfInitDeinit> rcfInit;

public:
	std::unique_ptr<RcfClient<I_HALbeADC> > adc_client;

private:

	Coordinate::IPv4 m_host;
	Coordinate::TCPPort m_port;

	// serialization needed to transfer the handle to the remote
	// (checking the USB serial on the server side seems like a good idea ;))
	friend class boost::serialization::access;
	template <typename Archiver>
	void serialize(Archiver& ar, unsigned int const);
#endif // PYPLUSPLUS
};     // class ADCRemoteHw

boost::shared_ptr<ADCRemoteHw> createADCRemoteHw();
boost::shared_ptr<ADCRemoteHw> createADCRemoteHw(Coordinate::IPv4 const& host,
                                                 Coordinate::TCPPort const& port,
                                                 HMF::ADC::USBSerial const& adc);
void freeADCRemoteHw(ADCRemoteHw& handle);

} // namespace Handle
} // namespace HMF

#ifndef PYPLUSPLUS
#include <boost/serialization/export.hpp>
BOOST_CLASS_EXPORT_KEY(::HMF::Handle::ADCRemoteHw)
#endif

// ----------------------------------------------------------------------------
//         ATMEL Crypto-Devices Software Support  -  Colorado Springs, CO -
// ----------------------------------------------------------------------------
// DISCLAIMER:  THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
// DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
// OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// ----------------------------------------------------------------------------

/** \file
 *  \brief 	This file contains implementations of SPI functions of the AES132 library.
 *  \author Atmel Crypto Products
 *  \date 	June 16, 2011
 */

#include <stdint.h>
#include <avr\io.h>
#include "aes132_spi.h"


// ----------------------------------------------------------------------------------
// ---- definitions and declarations of ports and pins CS\ pins are connected to ----
// ---------------------- Modify these to match your hardware. ----------------------
// ----------------------------------------------------------------------------------

/** \brief Adjust this variable considering how long one iteration of
 *         the inner while loop in spi_receive_bytes() takes
 *         (receiving one byte and decrementing the timeout counter).
 */
#define SPI_RX_TIMEOUT    ((uint8_t) 250)

/** \brief Adjust this variable considering how long one iteration of
 *         the inner while loop in spi_send_bytes() takes,
 *         (sending one byte and decrementing the timeout counter).
 */
#define SPI_TX_TIMEOUT    ((uint8_t)  50)


// When porting to a different target than an AVR 8-bit micro-controller,
// replace the port macros with the ones your target library uses.
// Add more macro definitions for the chip selects (e.g. SPI_CS_0)
// if you use more than one device.
#define SPI_PORT_OUT      PORTB   //!< port output register
#define SPI_PORT_DDR      DDRB    //!< port direction register
#define SPI_CS_0          (1)     //!< bit value for slave select of first device (pin 1)
#define SPI_CLOCK         (2)     //!< bit value for clock (pin 2)
#define SPI_MOSI          (4)     //!< bit value for Master-Out-Slave-In (pin 3)
#define SPI_MISO          (8)     //!< bit value for Master-In-Slave-Out (pin 4)

#define SPI_DEVICE_COUNT  (1)     //!< number of AES132 devices


// Port registers and pins for slave-select pins are listed in arrays
// to support more than one AES132 device on the same bus.
// A mix of several GPIO ports can be used for the slave-select signals.

//! array of SPI SS ports
static volatile uint8_t *spi_port[SPI_DEVICE_COUNT] = {&PORTB};

//! array of SPI SS direction registers
static volatile uint8_t *spi_ddr[SPI_DEVICE_COUNT] = {&DDRB};

//! array of SPI chip select pins
const static uint8_t spi_select[SPI_DEVICE_COUNT] = {SPI_CS_0};

/** \brief This variable holds the index for SPI port
 *         and chip select pin currently in use. Default is index 0.
 */
static uint8_t spi_current_index = 0;

// ----------------------------------------------------------------------------------
// ------------- end of ports and pins definitions and declarations -----------------
// ----------------------------------------------------------------------------------


/** \brief This function assigns the chip select pin to be used for communication.
 *
 *         The same SPI peripheral is used (clock and data lines), but for the
 *         chip select, a different port and / or pin can be assigned.
 * @param index index into the port and pin arrays
 * @return error if index argument is out of bounds
 */
uint8_t spi_select_device(uint8_t index)
{
	if (index >= SPI_DEVICE_COUNT)
		return AES132_FUNCTION_RETCODE_DEVICE_SELECT_FAIL;

	spi_current_index = index;

	return SPI_FUNCTION_RETCODE_SUCCESS;
}


/** This function selects the SPI slave. */
void spi_select_slave(void)
{
	SPI_PORT_OUT &= ~SPI_CLOCK;
	*spi_port[spi_current_index] &= ~spi_select[spi_current_index];
}


/** This function deselects the SPI slave. */
void spi_deselect_slave(void)
{
	SPI_PORT_OUT &= ~SPI_CLOCK;
	*spi_port[spi_current_index] |= spi_select[spi_current_index];
}


/** \brief This function initializes and enables the SPI peripheral. */
void spi_enable(void)
{
	volatile uint8_t io_register;

	// Set MOSI and SCK as output.
	SPI_PORT_DDR |= (SPI_MOSI | SPI_CLOCK);

	// Set SCK low.
	SPI_PORT_OUT &= ~SPI_CLOCK;

	// Set SS as output and high.
	*spi_ddr[spi_current_index] |= spi_select[spi_current_index];
	*spi_port[spi_current_index] |= spi_select[spi_current_index];

	// Enable pull-up resistor for MISO.
	SPI_PORT_OUT |= SPI_MISO;

	// Disable SPI power reduction.
	PRR0 &= ~(PRSPI << 1);

	// Enable SPI, Master, mode 0 (clock is low when idle, sample at leading edge).
	SPCR = (1 << SPE) | (1 << MSTR);

	// Set clock rate to maximum (F_CPU / 2).
	SPSR = (1 << SPI2X);

	// Clear SPIF bit in SPSR.
	io_register = SPSR;
	io_register = SPDR;
}


/** \brief This function disables the SPI peripheral. */
void spi_disable(void)
{
	// Disable SPI.
	SPCR &= ~(1 << SPE);

	// Enable SPI power reduction.
	PRR0 |= (PRSPI << 1);

	// Set MOSI and SCK, as input.
	SPI_PORT_DDR &= ~(SPI_MOSI | SPI_CLOCK);

	// Set SS as input.
	*spi_ddr[spi_current_index] &= ~spi_select[spi_current_index];
}


/** \brief This function sends bytes to an SPI device.
 *
 * \param[in] count number of bytes to send
 * \param[in] data pointer to tx buffer
 * \return status of the operation
 */
uint8_t spi_send_bytes(uint8_t count, uint8_t *data)
{
	uint8_t timeout;
	volatile uint8_t io_register;

	while (count-- > 0) {
		SPDR = *data++;
		timeout = SPI_TX_TIMEOUT;

		// Wait for transmission complete.
		while ((SPSR & (1 << SPIF)) == 0) {
			if (--timeout == 0)
				return SPI_FUNCTION_RETCODE_TIMEOUT;
		}

		// We could check here for the Write-Collision flag in SPSR,
		// but that would decrease the throughput.

		// Clear SPIF bit in SPSR by reading SPDR.
		// There is no need to read SPSR again since it was already
		// read with SPIF set in the while loop above.
		io_register = SPDR;
	}

	return SPI_FUNCTION_RETCODE_SUCCESS;
}


/** \brief This function receives bytes from an SPI device.
 *
 * \param[in] count number of bytes to receive
 * \param[in] data pointer to rx buffer
 * \return status of the operation
 */
uint8_t spi_receive_bytes(uint8_t count, uint8_t *data)
{
	uint8_t timeout;
	uint8_t writeDummy = 0;

	while (count-- > 0) {
		SPDR = writeDummy;
		timeout = SPI_RX_TIMEOUT;

		// Wait for byte.
		while ((SPSR & (1 << SPIF)) == 0) {
			if (--timeout == 0) {
				if (SPSR & (1 << SPIF))
					// Reset SPIF.
					writeDummy = SPDR;
				return AES132_FUNCTION_RETCODE_TIMEOUT;
			}
		}
		*data++ = SPDR;
	}

	return SPI_FUNCTION_RETCODE_SUCCESS;
}


//! write command id
#define AES132_SPI_WRITE        ((uint8_t) 2)

//! read command id
#define AES132_SPI_READ         ((uint8_t) 3)

//! enable-write command id
#define AES132_SPI_ENABLE_WRITE ((uint8_t) 6)

/** \brief three bytes (command id, word address MSB, word address LSB)
 *         before sending and receiving actual data
 */
#define AES132_SPI_PREFACE_SIZE ((uint8_t) 3)


/** \brief This function initializes and enables the SPI peripheral. */
void aes132p_enable_interface(void)
{
	spi_enable();
}


/** \brief This function disables the SPI peripheral. */
void aes132p_disable_interface(void)
{
	spi_disable();
}


/** \brief This function selects a device from an array of SPI chip select pins
 *         for AES132 devices on the same SPI bus.
 *
 * @param[in] device_id index into the SPI array for chip select ports and or pins
 * @return success or out-of-bounds error
 */
uint8_t aes132p_select_device(uint8_t device_id)
{
	return spi_select_device(device_id);
}


/** \brief This function writes bytes to the device.
 * \param[in] count number of bytes to write
 * \param[in] word_address word address to write to
 * \param[in] data pointer to tx buffer
 * \return status of the operation
 */
uint8_t aes132p_write_memory_physical(uint8_t count, uint16_t word_address, uint8_t *data)
{
	uint8_t aes132_lib_return;
	uint8_t writeEnable = AES132_SPI_ENABLE_WRITE;
	uint8_t preface[] =
		{AES132_SPI_WRITE, (uint8_t) (word_address >> 8), (uint8_t) (word_address & 0xFF)};

	// We don't need to enable write when writing to I/O address,
	// but an "if" condition would increase code space.
	spi_select_slave();
	aes132_lib_return = spi_send_bytes(1, &writeEnable);
	spi_deselect_slave();

	if (aes132_lib_return != AES132_FUNCTION_RETCODE_SUCCESS)
		return aes132_lib_return;

	// Here we could check the write-enable bit in the device status register,
	// but that would cost time and code space.
	spi_select_slave();
	aes132_lib_return = spi_send_bytes(AES132_SPI_PREFACE_SIZE, preface);
	if (aes132_lib_return == AES132_FUNCTION_RETCODE_SUCCESS)
		aes132_lib_return = spi_send_bytes(count, data);

	spi_deselect_slave();

	return aes132_lib_return;
}


/** \brief This function reads bytes from the device.
 * \param[in] size number of bytes to read
 * \param[in] word_address word address to read from
 * \param[out] data pointer to rx buffer
 * \return status of the operation
 */
uint8_t aes132p_read_memory_physical(uint8_t size, uint16_t word_address, uint8_t *data)
{
	uint8_t aes132_lib_return;
	uint8_t preface[] =
		{AES132_SPI_READ, (uint8_t) (word_address >> 8), (uint8_t) (word_address & 0xFF)};

	spi_select_slave();
	aes132_lib_return = spi_send_bytes(AES132_SPI_PREFACE_SIZE, preface);
	if (aes132_lib_return == AES132_FUNCTION_RETCODE_SUCCESS)
		aes132_lib_return = spi_receive_bytes(size, data);

	spi_deselect_slave();

	return aes132_lib_return;
}


/** \brief This function implements the interface for resynchronization
 * of communication, but cannot be implemented when using SPI.
 * \return always success
 */
uint8_t aes132p_resync_physical(void)
{
	return AES132_FUNCTION_RETCODE_SUCCESS;
}

#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/string_choices.h>

#include "rtl8261d.h"
#include "rtl8261x_patchs.h"

#define RTL8261C_VND2_PHY_STATE			0xA420

#define RTL8261C_VND2_PATCH_MODE_CTRL		0xB820
#define RTL8261C_VND2_PATCH_MODE_STAT		0xB800
#define RTL8261C_VND2_PATCH_CTRL		0xB82E
#define RTL8261C_PATCH_SETUP_VAL		0x6100

#define RTL8261C_VND2_INDIRECT_ADDR		0xA436
#define RTL8261C_VND2_INDIRECT_DATA		0xA438

#define RTL8261C_VND2_THERMAL_SENSOR_CTRL	0xB54C

int MmdPhyRead(struct phy_device *phydev, int devad, uint32_t regnum, uint16_t *val)
{
	if (!phydev)
		return -ENXIO;

	if (!val)
		return -EINVAL;

	uint32_t mmd = phy_read_mmd(phydev, devad, regnum);

	if ((mmd & 0x80000000) == 0)
	{
		*val = mmd;
		return 0;
	}
	return -EIO;
}

int MmdPhyWrite(struct phy_device *phydev, int devad, uint32_t regnum, uint16_t val)
{
	if (!phydev)
		return -ENXIO;
	return phy_write_mmd(phydev, devad, regnum, val);
}

int Nic_Rtl8261X_wait_for_bit(struct phy_device *phydev, uint32_t regnum, uint16_t mask, uint8_t expected_one)
{
	int timeout = 100;
	int ret;
	uint16_t value = 0;

	if (!phydev)
		return -ENXIO;

	do {
		ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, regnum, &value);
		if (ret) return ret;

		uint16_t check_value = value;

		if (!expected_one) 
			check_value = ~value;

		if ((mask & ~check_value) == 0) 
			return 0;

		mdelay(1);
		timeout--;
	} while (timeout > 0);

	return -EBUSY;
}

int Nic_Rtl8261X_phy_init(struct phy_device *phydev)
{
	int ret;
	uint16_t value;

	if (!phydev)
		return -ENXIO;

	ret = Nic_Rtl8261X_wait_for_bit(phydev, RTL8261C_VND2_PHY_STATE, 3, 1);
	if (ret) return ret;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xA400, 0x9200);
	if (ret) return ret;

	// check phy id
	uint32_t phy_id;
	ret = MmdPhyRead(phydev, 1, 2, &value);
	if (ret) return ret;
	phy_id = (uint32_t)value << 16;

	ret = MmdPhyRead(phydev, 1, 3, &value);
	if (ret) return ret;
	phy_id |= (uint32_t)value;

	if (phy_id - 0x1CC898 > 1 )
	{
		phydev_err(phydev, "Not rtl8261x and skip init flow...id = %x \n", phy_id);
		return -ENODEV;
	}

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x801E);
	if (ret) return ret;

	// check phy patch
	uint16_t patch_reg_val;
	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, &patch_reg_val);
	if (ret) return ret;

	if (patch_reg_val == 0x1515)
	{
		phydev_info(phydev, "RTL8261x Already patched. SKIP patch\n");
		return 0;
	}

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x801D);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, &patch_reg_val);
	if (ret) return ret;
	
	/* debug delete point */ phydev_info(phydev, "RTL8261x start patch\n");
	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_MODE_CTRL, &value);
	if (ret) return ret;
	value |= 0x10u;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_MODE_CTRL, value);
	if (ret) return ret;

	ret = Nic_Rtl8261X_wait_for_bit(phydev, RTL8261C_VND2_PATCH_MODE_STAT, 0x40, 1);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x8023);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, RTL8261C_PATCH_SETUP_VAL);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0xB82E);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 1);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_MODE_CTRL, 0x90);
	if (ret) return ret;

	/* debug delete point */ phydev_info(phydev, "RTL8261x patch80\n");
	for (int i=0; i<(sizeof(patch80)/sizeof(struct patch_16_16_16)); i++)
	{
		ret = MmdPhyWrite(phydev, patch80[i].devad, patch80[i].reg, patch80[i].val);
		if (ret) return ret;
	}

	/* debug delete point */ phydev_info(phydev, "RTL8261x patch85\n");
	for (int i=0; i<(sizeof(patch85)/sizeof(struct patch_16_16_16)); i++)
	{
		ret = MmdPhyWrite(phydev, patch85[i].devad, patch85[i].reg, patch85[i].val);
		if (ret) return ret;
	}

	/* debug delete point */ phydev_info(phydev, "RTL8261x patch250\n");
	for (int i=0; i<(sizeof(patch250)/sizeof(struct patch_16_16_16)); i++)
	{
		ret = MmdPhyWrite(phydev, patch250[i].devad, patch250[i].reg, patch250[i].val);
		if (ret) return ret;
	}

	/* debug delete point */ phydev_info(phydev, "RTL8261x patch626\n");
	for (int i=0; i<(sizeof(patch626)/sizeof(struct patch_16_16_16)); i++)
	{
		ret = MmdPhyWrite(phydev, patch626[i].devad, patch626[i].reg, patch626[i].val);
		if (ret) return ret;
	}

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0xB87C);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x8FED);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0xB87E);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x4EFF);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_MODE_CTRL, &value);
	if (ret) return ret;
	value &= ~0x80u;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_MODE_CTRL, value);
	if (ret) return ret;

	/* debug delete point */ phydev_info(phydev, "RTL8261x patch819\n");
	for (int i=0; i<(sizeof(patch819)/sizeof(struct patch_16_16_16)); i++)
	{
		ret = MmdPhyWrite(phydev, patch819[i].devad, patch819[i].reg, patch819[i].val);
		if (ret) return ret;
	}

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x8173);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x8620);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x8175);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x8671);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x8370);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x8671);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x8372);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x86C8);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x8401);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x86C8);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x8403);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x86DA);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_CTRL, &value);
	if (ret) return ret;
	value &= ~1u;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_CTRL, value);
	if (ret) return ret;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x8023);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_MODE_CTRL, &value);
	if (ret) return ret;
	value &= ~0x10u;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_PATCH_MODE_CTRL, value);
	if (ret) return ret;

	ret = Nic_Rtl8261X_wait_for_bit(phydev, RTL8261C_VND2_PATCH_MODE_STAT, 0x40, 0);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0x3C, &value);
	if (ret) return ret;
	value &= 0xFFF1u;
	ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0x3C, value);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0x3E, &value);
	if (ret) return ret;
	value &= 0xFFFCu;
	ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0x3E, value);
	if (ret) return ret;


	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA432, &value);
	if (ret) return ret;
	value |= 0x20u;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xA432, value);
	if (ret) return ret;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_ADDR, 0x801E);
	if (ret) return ret;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_INDIRECT_DATA, 0x1515);
	if (ret) return ret;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xA400, 0x9200);
	if (ret) return ret;

	phydev_info(phydev, "RTL8261X init done !!\n");
	return 0;
}

int Nic_Rtl8261X_ThermalSensorEnable_set(struct phy_device *phydev, bool enable, uint16_t a3)
{
	int ret;
	uint16_t value;

	if (a3 > 120)
		return -EINVAL;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_THERMAL_SENSOR_CTRL, &value);
	if (ret) return ret;
	value = value & 0xFFF7;

	if (enable)
		value = value | 8;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_THERMAL_SENSOR_CTRL, value);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_THERMAL_SENSOR_CTRL, &value);
	if (ret) return ret;
	value &= 0x3F;
	value |= (a3 << 7);

	return MmdPhyWrite(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_THERMAL_SENSOR_CTRL, value);
}

int Nic_Rtl8261X_ThermalSensorEnable_get(struct phy_device *phydev, bool *enabled)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, RTL8261C_VND2_THERMAL_SENSOR_CTRL, &value);
	if (ret) return ret;

	*enabled = (value & 8) != 0;
	return 0;
}

int Nic_Rtl8261X_serdes_option_set(struct phy_device *phydev, uint16_t option, uint16_t val)
{
	int ret;

	uint16_t value;
	uint16_t val_to_set;
	uint16_t reg_addr;
	uint16_t value_mask;

	if (!phydev)
		return -ENXIO;

	switch (option)
	{
	case 0:
		reg_addr = 0x6977;
		val_to_set = val & 0xF;
		value_mask = 0xFFA0;
		break;
	case 1:
		reg_addr = 0x6976;
		val_to_set = val & 0xF;
		value_mask = 0xFFA0;
		break;
	case 2:
		reg_addr = 0x6975;
		val_to_set = val & 0xF;
		value_mask = 0xFFA0;
		break;
	case 3:
		reg_addr = 0x6976;
		val_to_set = val & 0xF00;
		value_mask = 0xA0FF;
		break;
	case 4:
		reg_addr = 0x6972;
		val_to_set = val & 0xF;
		value_mask = 0xFFA0;
		break;
	case 5:
		reg_addr = 0x6974;
		val_to_set = val & 0xF;
		value_mask = 0xFFA0;
		break;
	case 6:
		reg_addr = 0x6973;
		val_to_set = val & 0xF;
		value_mask = 0xFFA0;
		break;
	case 7:
		reg_addr = 0x6974;
		val_to_set = val & 0xF00;
		value_mask = 0xA0FF;
		break;
	case 8:
		reg_addr = 0x6973;
		val_to_set = val & 0xF00;
		value_mask = 0xA0FF;
		break;
	case 9:
		reg_addr = 0x6972;
		val_to_set = val & 0xF00;
		value_mask = 0xA0FF;
		break;
	default:
		return -EINVAL;
	}

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND1, reg_addr, &value);
	if (ret) return ret;

	uint16_t new_value = (value & value_mask) | val_to_set;
	return MmdPhyWrite(phydev, MDIO_MMD_VEND1, reg_addr, new_value);
}

int Nic_Rtl8261X_usxgmii_autoNego_set(struct phy_device *phydev, bool enable)
{
	int ret;
	int timeout = 500;
	uint16_t value;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND1, 0x7588, 0xF1);
	if (ret) return ret;

	value = (enable != 0) ? 0x854F : 0x854E;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND1, 0x7589, value);
	if (ret) return ret;
	
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND1, 0x7587, 3);
	if (ret) return ret;

	do {
		ret = MmdPhyRead(phydev, MDIO_MMD_VEND1, 0x7587, &value);
		if (ret) return ret;
		if ((value & 0x0001) == 0)
			return 0;
	} while (timeout > 0);

	return -ETIMEDOUT;
}

int Nic_Rtl8261X_enable_set(struct phy_device *phydev, bool enable)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, 1, 0, &value);
	if (ret) return ret;

	if (enable != 0)
		value = value & 0xF7FF;
	else
		value = value | 0x0800;

	return MmdPhyWrite(phydev, 1, 0, value);
}

int Nic_Rtl8261X_autoNegoAbility_set(struct phy_device *phydev, rtk_autoNegoAbility_cfg_t *config)
{
	int ret;
	uint16_t value;

	// MDIO_MMD_AN MDIO_AN_ADVERTISE
	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0x10, &value);
	if (ret) return ret;
	value = (value & 0xF21F) |
			((config->a10base_te_half & 1) << 5) |
			((config->a10base_te_full & 1) << 6) |
			((config->a100base_tx_half & 1) << 7) |
			((config->a100base_tx_full & 1) << 8) |
			((config->apause & 1) << 11) |
			((config->pause & 1) << 10);
	ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0x10, value);
	if (ret) return ret;


	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA412, &value);
	if (ret) return ret;
	value = (value & 0xFDFF) | ((config->unknow_bit4 & 1) << 9);
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xA412, value);
	if (ret) return ret;


	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0x20, &value);
	if (ret) return ret;

	value = (value & 0xEE7F) | 
			((config->a2500base_t & 1) << 7) |
			((config->a5000base_t & 1) << 8) |
			((config->a10Gbase_t & 1) << 12);

	ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0x20, value);
	if (ret) return ret;

	// MDIO_MMD_AN AN Control Reg
	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0, &value);
	if (ret) return ret;

	// AN Enabled
	if ((value & 0x1000) != 0)
	{	
		// AN Restart
		value |= 0x0200;
		ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0, value);
		if (ret) return ret;
	}

	return 0;
}

int Nic_Rtl8261X_autoNegoEnable_set(struct phy_device *phydev, bool enable)
{
	int ret;
	uint16_t value;

	// AN Enabled
	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0, &value);
	if (ret) return ret;
	value = value & 0xEFFF;
	if (enable)
		value = value | 0x1000;
	ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0, value);
	if (ret) return ret;

	// AN Restart
	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0, &value);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0, value | 0x200);
	if (ret) return ret;

	return 0;
}

int Nic_Rtl8261X_autoNegoEnable_get(struct phy_device *phydev, bool *enable)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0, &value);
	if (ret) return ret;

	*enable = (value & 0x1000) != 0;
	return 0;
}

int Nic_Rtl8261X_eeeEnable_set(struct phy_device *phydev, rtk_eee_cfg_t *config)
{
	int ret;
	uint16_t value;
	bool autoneo_enabled = false;

	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0x3C, &value);
	if (ret) return ret;

	value = (value & 0xFFF1) |
			((config->e100base_tx & 1) << 1) |
			((config->e1000base_tx & 1) << 2) |
			((config->e10Gbase_t & 1) << 3);

	ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0x3C, value);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0x3E, &value);
	if (ret) return ret;

	value = (value & 0xFFFC) |
			((config->e2500base_t & 1) << 0) |
			((config->e5000base_t & 1) << 1);
	ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0x3E, value);
	if (ret) return ret;

	ret = Nic_Rtl8261X_autoNegoEnable_get(phydev, &autoneo_enabled);
	if (ret) return ret;
	if (autoneo_enabled)
	{
		ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0, &value);
		if (ret) return ret;
		ret = MmdPhyWrite(phydev, MDIO_MMD_AN, 0, value | 0x200);
		if (ret) return ret;
	}
	return 0;
}

int Nic_Rtl8261X_improve_antiInterference_capability(struct phy_device *phydev)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xBF88, &value);
	if (ret) return ret;
	value |= 0x0004;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xBF88, value);
	if (ret) return ret;
	

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xBF8A, &value);
	if (ret) return ret;
	value = (value & 0x03FF) | 0x2000;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xBF8A, value);
	if (ret) return ret;
	return 0;
}

int Nic_Rtl8261X_intr_get_read_clear(struct phy_device *phydev, uint16_t *val)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA4D4, &value);
	if (ret) return ret;

	*val = value;
	return 0;
}

int Nic_Rtl8261X_linkDownPowerSavingEnable_get(struct phy_device *phydev, bool *enabled)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA430, &value);
	if (ret) return ret;

	*enabled = (value & 4) != 0;
	return 0;
}

int Nic_Rtl8261X_linkDownPowerSavingEnable_set(struct phy_device *phydev, bool enable)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA430, &value);
	if (ret) return ret;
	value = value & 0xFFFB;

	if (enable)
		value = value | 4;
	return MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xA430, value);
}

int Nic_Rtl8261X_loopback_set(struct phy_device *phydev, bool enable)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, 3, 0, &value);
	if (ret) return ret;
	value = value & 0xBFFF;
    if (enable)
		value = value | 0x4000;
	return MmdPhyWrite(phydev, 3, 0, value);
}

int Nic_Rtl8261X_is_link(struct phy_device *phydev, bool *is_linked)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA402, &value);
	if (ret) return ret;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA402, &value);
	if (ret) return ret;

	*is_linked = (value & 4) != 0;
	return 0;
}

int Nic_Rtl8261X_duplex_get(struct phy_device *phydev, bool *duplex)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA434, &value);
	if (ret) return ret;

	*duplex = (value & 8) != 0;
	return 0;
}

int Nic_Rtl8261X_speed_get(struct phy_device *phydev, uint64_t *speed)
{
	int ret;
	uint16_t value;

	bool is_linked;
	ret = Nic_Rtl8261X_is_link(phydev, &is_linked);
	if (ret) return ret;

	if (!is_linked)
	{
		*speed = 0;
		return 0;
	}

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA434, &value);
	if (ret) return ret;

	uint16_t v5 = (value >> 8) & 0x0003;
	uint16_t v6 = (value >> 4) & 0x0003;

	switch (v5)
	{
	case 0:
		switch (v6) {
            case 0: *speed = 10; break;
            case 1: *speed = 100; break;
            case 2: *speed = 1000; break;
            case 3: *speed = 500; break;
            default: return -EIO;
        }
		break;
	case 1:
		switch (v6) {
            case 0: *speed = 10000; break;
            case 1: *speed = 2500; break;
            case 2: *speed = 5000; break;
            case 3: *speed = 1000; break;
            default: return -EIO;
        }
		break;
	case 2:
		switch (v6) {
            case 0: *speed = 2500; break;
            case 1: *speed = 5000; break;
            default: return -EIO;
        }
		break;
	default:
		return -EIO;
	}
	return 0;
}

int Nic_Rtl8261X_serdes_link_get(struct phy_device *phydev, bool *is_linked, int *link_state)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND1, 0x758D, &value);
	if (ret) return ret;

    *is_linked = (value & 0x0002) != 0;

    ret = MmdPhyRead(phydev, MDIO_MMD_VEND1, 0x7580, &value);
	if (ret) return ret;

    unsigned int v7 = value & 0x001F;
    unsigned int v8 = (value >> 10) & 0x001F;

    switch (v7) {
        case 2:
            *link_state = 7;
            break;
        case 4:
            *link_state = 8;
            break;
        case 18:
            *link_state = 6;
            break;
        case 22:
            *link_state = 5;
            break;
        case 21:
            *link_state = 3;
            break;
        case 13:
            *link_state = 0;
            break;
        case 26:  // 0x1A
            if (v8 == 0) {
                *link_state = 2;
            } else if (v8 == 1) {
                *link_state = 1;
            } else if (v8 == 2) {
                *link_state = 4;
            } else {
                *link_state = 9;
            }
            break;
        case 31:
            *link_state = 10;
            break;
        default:
            *link_state = 9;
            break;
    }
    return 0;
}

int Nic_Rtl8261X_serdes_autoNego_set(struct phy_device *phydev, bool enable)
{
	int ret;
	uint16_t value;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND1, 0x7588, 2);
	if (ret) return ret;

	value = enable ? 0x70D0 : 0x71D0;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND1, 0x7589, value);
	if (ret) return ret;
	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND1, 0x7587, 3);
	if (ret) return ret;

    return 0;
}

int Nic_Rtl8261X_masterSlave_set(struct phy_device *phydev, int mode)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_AN, 0x20, &value);
	if (ret) return ret;

	value = value & 0x3FFF;

	switch (mode) {
        case 0: // Disable MASTER-SLAVE manual config
            break; // 00
        case 1: // Master mode
            value = value | 0x8000; // 10
            break;
        case 2: // Slave mode
            value = value | 0xC000; // 11
            break;
        default: return -EINVAL;
    }
	return MmdPhyWrite(phydev, MDIO_MMD_AN, 0x20, value);
}

int Nic_Rtl8261X_masterSlave_get(struct phy_device *phydev, int *mode)
{
	// what fuxk is this????
	// __int64 result; // x0
	// unsigned __int16 v5; // w3
	// __int16 value; // [xsp+2Eh] [xbp+2Eh] BYREF

	// value = 0;
	// result = (unsigned __int8)MmdPhyRead(a1, MDIO_MMD_AN, 0x20, &value);
	// if ( (_DWORD)result )
	// {
	// 	v5 = value & 0x3FFF;
	// 	value &= 0x3FFFu;
	// 	if ( a2 == 1 )
	// 	{
	// 	value = v5 | 0x8000;
	// 	return MmdPhyWrite(a1, MDIO_MMD_AN, 0x20, v5 | 0x8000);
	// 	}
	// 	if ( a2 )
	// 	{
	// 	result = 0;
	// 	if ( a2 != 2 )
	// 		return result;
	// 	v5 |= 0xC000u;
	// 	value = v5;
	// 	}
	// 	return MmdPhyWrite(a1, MDIO_MMD_AN, 0x20, v5);
	// }
	return 0;
}

int Nic_Rtl8261X_crossOverMode_set(struct phy_device *phydev, int mode)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA430, &value);
	if (ret) return ret;
	value = value & 0xFCFF;

	switch (mode)
	{
	case 0:
		/* code */
		break;
	case 1:
		value = value | 0x300;
		break;
	case 2:
		value = value | 0x200;
		break;
	default: return -EINVAL;
	}

	return MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xA430, value);
}
int Nic_Rtl8261X_crossOverStatus_get(struct phy_device *phydev, int *mode)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xA434, &value);
	if (ret) return ret;

    *mode = ((value ^ 2uLL) >> 1) & 1;
	return 0;
}

int rtl8261x_set_tunable(struct phy_device *phydev,
			    struct ethtool_tunable *tuna,
			    const void *data)
{
	if (tuna->id != 3)
		return -EOPNOTSUPP;

	if (data && *((uint32_t*)data))
	{
		if (*((uint32_t*)data) == 0xFFFF)
		{
			Nic_Rtl8261X_linkDownPowerSavingEnable_set(phydev, 1);
			return 0;
		}
		return -EINVAL;
	}
	return Nic_Rtl8261X_linkDownPowerSavingEnable_set(phydev, 0);
}

int rtl8261x_get_tunable(struct phy_device *phydev,
			   struct ethtool_tunable *tuna, void *data)
{
	int ret;
	bool enable;
	if (tuna->id != 3)
		return -EOPNOTSUPP;

	ret = Nic_Rtl8261X_linkDownPowerSavingEnable_get(phydev, &enable);
	if (ret) return ret;
	*((uint16_t*)data) = -enable;

	return 0;
}

int rtl8261d_set_led_blink_mode(struct phy_device *phydev, uint16_t mode, uint16_t led_id)
{
	int ret;
	uint16_t value;

	uint16_t led_reg = 2 * ((led_id + 0x6819) & 0x7FFF);
	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, led_reg, &value);
	if (ret) return ret;

	ret = MmdPhyWrite(phydev, MDIO_MMD_VEND2, led_reg, (mode & 0x3FF) | (value & 0xFC00));
	if (ret) return ret;
	return 0;
}

int rtl8261x_config_init(struct phy_device *phydev)
{
	int ret;
	phydev_info(phydev, "rtl8261x_config_init Start\n", ret);

	ret = Nic_Rtl8261X_phy_init(phydev);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_phy_init failed: %d\n", ret);
		return ret;
	}

	ret = Nic_Rtl8261X_ThermalSensorEnable_set(phydev, 0, 0x78);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_ThermalSensorEnable_set failed: %d\n", ret);
		return ret;
	}

	// not sure (phydev, 4, 0) or (phydev, 4, 4) ???
	ret = Nic_Rtl8261X_serdes_option_set(phydev, 4, 0);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_serdes_option_set failed: %d\n", ret);
		return ret;
	}

	ret = Nic_Rtl8261X_usxgmii_autoNego_set(phydev, 1);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_usxgmii_autoNego_set failed: %d\n", ret);
		return ret;
	}

	ret = Nic_Rtl8261X_enable_set(phydev, 0);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_enable_set failed: %d\n", ret);
		return ret;
	}

	rtk_autoNegoAbility_cfg_t ang_cfg = {
		.a100base_tx_full = 1,
		.a100base_tx_half = 1,
		.a10base_te_full = 1,
		.a10base_te_half = 1,
		.a10Gbase_t = 1,
		.a2500base_t = 1,
		.a5000base_t = 1,
		.apause = 1,
		.pause = 1
	};

	ret = Nic_Rtl8261X_autoNegoAbility_set(phydev, &ang_cfg);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_autoNegoAbility_set failed: %d\n", ret);
		return ret;
	}

	ret = Nic_Rtl8261X_autoNegoEnable_set(phydev, 1);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_autoNegoEnable_set failed: %d\n", ret);
		return ret;
	}

	ret = Nic_Rtl8261X_enable_set(phydev, 1);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_enable_set failed: %d\n", ret);
		return ret;
	}

	rtk_eee_cfg_t eee_cfg = {0};

	ret = Nic_Rtl8261X_eeeEnable_set(phydev, &eee_cfg);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_eeeEnable_set failed: %d\n", ret);
		return ret;
	}

	ret = Nic_Rtl8261X_improve_antiInterference_capability(phydev);
	if (ret){
		phydev_err(phydev, "Nic_Rtl8261X_improve_antiInterference_capability failed: %d\n", ret);
		return ret;
	}

	ret = rtl8261d_set_led_blink_mode(phydev, 1023, 3);
	if (ret){
		phydev_err(phydev, "rtl8261d_set_led_blink_mode failed: %d\n", ret);
		return ret;
	}
	phydev_info(phydev, "rtl8261x_config_init Finish...\n", ret);

	return 0;
}

int rtl8261x_get_features(struct phy_device *phydev)
{
    int ret;
    ret = genphy_c45_pma_read_abilities(phydev);
    if (ret) return ret;

    linkmode_or(phydev->supported, phydev->supported, PHY_BASIC_FEATURES);

    linkmode_set_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
                       phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
                       phydev->supported);
    return 0;
}

int rtl8261x_suspend(struct phy_device *phydev)
{
    int ret = 0;

    ret = Nic_Rtl8261X_enable_set(phydev, 0);
    if (ret) return ret;

    phydev->speed = SPEED_UNKNOWN;
    phydev->duplex = DUPLEX_UNKNOWN;
    phydev->pause = 0;
    phydev->asym_pause = 0;

    return ret;
}

int rtl8261x_resume(struct phy_device *phydev)
{
	return Nic_Rtl8261X_enable_set(phydev, 1);
}

int rtl8261x_config_aneg(struct phy_device *phydev)
{
    bool changed = false;
    u16 reg = 0;
    int ret = 0;

    phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
    if (phydev->autoneg == AUTONEG_DISABLE)
        return genphy_c45_pma_setup_forced(phydev);

    ret = genphy_c45_an_config_aneg(phydev);
    if (ret < 0)
        return ret;
    if (ret > 0)
        changed = true;

    reg = 0;
    if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
                  phydev->advertising))
        reg |= BIT(9);

    if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
                  phydev->advertising))
        reg |= BIT(8);

    ret = phy_modify_mmd_changed(phydev, MDIO_MMD_VEND2, 0xA412,
                     BIT(9) | BIT(8) , reg);
    if (ret < 0)
        return ret;
    if (ret > 0)
        changed = true;

    return genphy_c45_check_and_restart_aneg(phydev, changed);
}

int rtl8261x_aneg_done(struct phy_device *phydev)
{
    return genphy_c45_aneg_done(phydev);
}

int rtl8261x_read_status(struct phy_device *phydev)
{
    int ret = 0, status = 0;
    phydev->speed = SPEED_UNKNOWN;
    phydev->duplex = DUPLEX_UNKNOWN;
    phydev->pause = 0;
    phydev->asym_pause = 0;

    ret = genphy_c45_read_link(phydev);
    if (ret)
        return ret;

    if (phydev->autoneg == AUTONEG_ENABLE)
    {
        linkmode_clear_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
           phydev->lp_advertising);

        ret = genphy_c45_read_lpa(phydev);
        if (ret)
            return ret;

        status =  phy_read_mmd(phydev, 31, 0xA414);
        if (status < 0)
            return status;
        linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
            phydev->lp_advertising, status & BIT(11));

        phy_resolve_aneg_linkmode(phydev);
    }
    else
    {
        ret = genphy_c45_read_pma(phydev);
    }

    /* mdix*/
    status = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_10GBT_SWAPPOL);
    if (status < 0)
        return status;

    switch (status & 0x3)
    {
        case MDIO_PMA_10GBT_SWAPPOL_ABNX | MDIO_PMA_10GBT_SWAPPOL_CDNX:
            phydev->mdix = ETH_TP_MDI;
            break;

        case 0:
            phydev->mdix = ETH_TP_MDI_X;
            break;

        default:
            phydev->mdix = ETH_TP_MDI_INVALID;
            break;
    }

    return ret;
}

int rtl8261x_ack_intr(struct phy_device *phydev)
{
	int ret;
	uint16_t value = 0;

	ret = Nic_Rtl8261X_intr_get_read_clear(phydev, &value);
	if (ret) return ret;

	return 0;
}

int rtl8261x_config_intr(struct phy_device *phydev)
{
	int ret;
	uint16_t value;

	ret = MmdPhyRead(phydev, MDIO_MMD_VEND2, 0xD20E, &value);
	if (ret) return ret;

    value = value & 0xFFEF;
    if (phydev->interrupts)
		value = value | 0x10;
    return MmdPhyWrite(phydev, MDIO_MMD_VEND2, 0xD20E, value);
}

irqreturn_t rtl8261x_handle_intr(struct phy_device *phydev)
{
	int ret;
	uint16_t value;	

	ret = Nic_Rtl8261X_intr_get_read_clear(phydev, &value);
	if (ret) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if ((value & 0x10) == 0)
		return IRQ_NONE;
	
	phy_mac_interrupt(phydev);
	return IRQ_HANDLED;
}

int rtl8261x_set_loopback(struct phy_device *phydev, bool enable)
{
	return Nic_Rtl8261X_loopback_set(phydev, enable);
}

static int rtl8261x_match_phy_device_c45(struct phy_device *phydev)
{
	u32 rid;
	rid = phydev->c45_ids.device_ids[1];
	if ((rid == 0xffffffff) && phydev->mdio.bus->read_c45) {
		int val;

		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PKGID1);
		if (val < 0)
			return 0;

		rid = val << 16;
		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PKGID2);
		if (val < 0)
			return 0;

		rid |= val;
	}

	return (PHYID_RTL8261D_CG == rid);
}

static struct phy_driver rtl8261_drv[] = {
	{
		.match_phy_device = rtl8261x_match_phy_device_c45,
		.name		      = "RTL8261D Gigabit Ethernet",
		.get_features     = rtl8261x_get_features,
		// .probe            = rtl822x_probe,
		.config_init      = rtl8261x_config_init,
		.config_aneg      = rtl8261x_config_aneg,
		.aneg_done        = rtl8261x_aneg_done,
		.read_status      = rtl8261x_read_status,
		.config_intr      = rtl8261x_ack_intr,
		.handle_interrupt = rtl8261x_handle_intr,
		.suspend	      = rtl8261x_suspend,
		.resume		      = rtl8261x_resume,
		.get_tunable      = rtl8261x_get_tunable,
		.set_tunable      = rtl8261x_set_tunable,
		.set_loopback     = rtl8261x_set_loopback,
	}
};

module_phy_driver(rtl8261_drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("StarField Xu<airjinkela@163.com>");
MODULE_DESCRIPTION("RTL8261d 10GbE phy driver for MT7988");

#ifndef QBRT_RESOURCETYPE_H
#define QBRT_RESOURCETYPE_H

#define RESOURCE_IMPORT		0x01
#define RESOURCE_STRING		0x02
#define RESOURCE_CONSTRUCT	0x03
#define RESOURCE_FUNCTION	0x04
#define RESOURCE_MODSYM		0x05
#define RESOURCE_HASHTAG	0x06
#define RESOURCE_DATATYPE	0x07
#define RESOURCE_TYPESPEC	0x08
#define RESOURCE_PROTOCOL	0x09
#define RESOURCE_POLYMORPH	0x0A

// Function Context Enums
#define PFC_NULL	0b000
#define PFC_NONE	0b001
#define PFC_ABSTRACT	0b010
#define PFC_DEFAULT	0b011
#define PFC_OVERRIDE	0b111

#define PFC_MASK_HAS_CODE 0b001
#define PFC_MASK_PROTOCOL 0b010
#define PFC_MASK_OVERRIDE 0b100

// Function Context Types (does not include code bit)
#define FCT_TRADITIONAL	0b000
#define FCT_PROTOCOL	0b010
#define FCT_POLYMORPH	0b110

#define PFC_TYPE(pfc)		(pfc & 0b110)
#define FCT_WITH_CODE(fct)	(fct | 0b001)

#endif

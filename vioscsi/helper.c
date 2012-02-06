/**********************************************************************
 * Copyright (c) 2012  Red Hat, Inc.
 *
 * File: helper.c
 *
 * Author(s):
 * Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This file contains various virtio queue related routines.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "helper.h"
#include "utils.h"


BOOLEAN
SynchronizedSRBRoutine(
    IN PVOID DeviceExtension,
    IN PVOID Context
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_REQUEST_BLOCK Srb      = (PSCSI_REQUEST_BLOCK) Context;
    PSRB_EXTENSION srbExt        = (PSRB_EXTENSION)Srb->SrbExtension;

ENTER_FN();
    if (adaptExt->pci_vq_info[2].vq->vq_ops->add_buf(adaptExt->pci_vq_info[2].vq,
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->cmd) >= 0){
        adaptExt->pci_vq_info[2].vq->vq_ops->kick(adaptExt->pci_vq_info[2].vq);
        return TRUE;
    }
    Srb->SrbStatus = SRB_STATUS_BUSY;
    StorPortBusy(DeviceExtension, adaptExt->queue_depth);
EXIT_ERR();
    return FALSE;
}

BOOLEAN
SendSRB(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN();
    return StorPortSynchronizeAccess(DeviceExtension, SynchronizedSRBRoutine, (PVOID)Srb);
EXIT_FN();
}

BOOLEAN
SynchronizedTMFRoutine(
    IN PVOID DeviceExtension,
    IN PVOID Context
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_REQUEST_BLOCK Srb      = (PSCSI_REQUEST_BLOCK) Context;
    PSRB_EXTENSION srbExt        = (PSRB_EXTENSION)Srb->SrbExtension;

ENTER_FN();
    if (adaptExt->pci_vq_info[0].vq->vq_ops->add_buf(adaptExt->pci_vq_info[0].vq,
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->cmd) >= 0){
        adaptExt->pci_vq_info[0].vq->vq_ops->kick(adaptExt->pci_vq_info[0].vq);
        return TRUE;
    }
    Srb->SrbStatus = SRB_STATUS_BUSY;
    StorPortBusy(DeviceExtension, adaptExt->queue_depth);
EXIT_ERR();
    return FALSE;
}

BOOLEAN
SendTMF(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN();
    return StorPortSynchronizeAccess(DeviceExtension, SynchronizedTMFRoutine, (PVOID)Srb);
EXIT_FN();
}

BOOLEAN
DeviceReset(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_REQUEST_BLOCK   Srb = &adaptExt->tmf_cmd.Srb;
    PSRB_EXTENSION        srbExt = adaptExt->tmf_cmd.SrbExtension;
    VirtIOSCSICmd         *cmd = &srbExt->cmd;
    ULONG                 fragLen;
    ULONG                 sgElement;

ENTER_FN();
    if (adaptExt->dump_mode) {
        return TRUE;
    }
    ASSERT(adaptExt->tmf_infly == FALSE);

    memset((PVOID)cmd, 0, sizeof(VirtIOSCSICmd));
    cmd->sc = Srb;
    cmd->req.tmf.lun[0] = 1;
    cmd->req.tmf.lun[1] = 0;
    cmd->req.tmf.lun[2] = 0;
    cmd->req.tmf.lun[3] = 0;
    cmd->req.tmf.type = VIRTIO_SCSI_T_TMF;
    cmd->req.tmf.subtype = VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET;

    sgElement = 0;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->req.tmf, &fragLen);
    srbExt->sg[sgElement].ulSize   = sizeof(cmd->req.tmf);
    sgElement++;
    srbExt->out = sgElement;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->resp.tmf, &fragLen);
    srbExt->sg[sgElement].ulSize   = sizeof(cmd->resp.tmf);
    sgElement++;
    srbExt->in = sgElement - srbExt->out;
    StorPortPause(DeviceExtension, 60);
    if (!SendTMF(DeviceExtension, Srb)) {
        StorPortResume(DeviceExtension);
        return FALSE;
    }
    adaptExt->tmf_infly = TRUE;
    return TRUE;
}

VOID
ShutDown(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
ENTER_FN();
    VirtIODeviceReset(DeviceExtension);
    StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_GUEST_FEATURES), 0);

EXIT_FN();
}

VOID
GetScsiConfig(
    IN PVOID DeviceExtension
)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
ENTER_FN();

    adaptExt->features = StorPortReadPortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_HOST_FEATURES));

    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, seg_max),
                      &adaptExt->scsi_config.seg_max, sizeof(adaptExt->scsi_config.seg_max));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, num_queues),
                      &adaptExt->scsi_config.num_queues, sizeof(adaptExt->scsi_config.num_queues));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, max_sectors),
                      &adaptExt->scsi_config.max_sectors, sizeof(adaptExt->scsi_config.max_sectors));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, cmd_per_lun),
                      &adaptExt->scsi_config.cmd_per_lun, sizeof(adaptExt->scsi_config.cmd_per_lun));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, event_info_size),
                      &adaptExt->scsi_config.event_info_size, sizeof(adaptExt->scsi_config.event_info_size));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, sense_size),
                      &adaptExt->scsi_config.sense_size, sizeof(adaptExt->scsi_config.sense_size));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, cdb_size),
                      &adaptExt->scsi_config.cdb_size, sizeof(adaptExt->scsi_config.cdb_size));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, max_channel),
                      &adaptExt->scsi_config.max_channel, sizeof(adaptExt->scsi_config.max_channel));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, max_target),
                      &adaptExt->scsi_config.max_target, sizeof(adaptExt->scsi_config.max_target));
    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(VirtIOSCSIConfig, max_lun),
                      &adaptExt->scsi_config.max_lun, sizeof(adaptExt->scsi_config.max_lun));

EXIT_FN();
}

BOOLEAN
InitHW(
    IN PVOID DeviceExtension, 
    PPORT_CONFIGURATION_INFORMATION ConfigInfo
    )
{
    PACCESS_RANGE      accessRange;
    PADAPTER_EXTENSION adaptExt;

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    accessRange = &(*ConfigInfo->AccessRanges)[0];

    ASSERT (FALSE == accessRange->RangeInMemory) ;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Port  Resource [%08I64X-%08I64X]\n",
                accessRange->RangeStart.QuadPart,
                accessRange->RangeStart.QuadPart +
                accessRange->RangeLength));

    if ( accessRange->RangeLength < IO_PORT_LENGTH) {
        StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Wrong access range %x bytes\n", accessRange->RangeLength));
        return FALSE;
    }

    if (!StorPortValidateRange(DeviceExtension,
                                           ConfigInfo->AdapterInterfaceType,
                                           ConfigInfo->SystemIoBusNumber,
                                           accessRange->RangeStart,
                                           accessRange->RangeLength,
                                           (BOOLEAN)!accessRange->RangeInMemory)) {

        StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Range validation failed %x for %x bytes\n",
                   (*ConfigInfo->AccessRanges)[0].RangeStart.LowPart,
                   (*ConfigInfo->AccessRanges)[0].RangeLength));

        return FALSE;
    }
    adaptExt->device_base = (ULONG_PTR)StorPortGetDeviceBase(DeviceExtension,
                                           ConfigInfo->AdapterInterfaceType,
                                           ConfigInfo->SystemIoBusNumber,
                                           accessRange->RangeStart,
                                           accessRange->RangeLength,
                                           (BOOLEAN)!accessRange->RangeInMemory);

    if (adaptExt->device_base == (ULONG_PTR)NULL) {
        StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Couldn't map %x for %x bytes\n",
                   (*ConfigInfo->AccessRanges)[0].RangeStart.LowPart,
                   (*ConfigInfo->AccessRanges)[0].RangeLength));
        return FALSE;
    }

EXIT_FN();
    return TRUE;
}

#include <stdlib.h>
#include "z64.h"
#include "macros.h"
#include "libultraship/libultra/gbi.h"
#include "../soh/assets/textures/parameter_static/parameter_static.h"
#include "variables.h"
#include "custom-message/CustomMessageTypes.h"
#include "functions.h"


#include "luslog.h"

#define DIGIT_WIDTH 8
#define DIGIT_HEIGHT 16
#define HUNDREDS_POSITION_OFFSET -16
#define TENS_POSITION_OFFSET 8
#define ONES_POSITION_OFFSET 8
#define BLINK_DURATION 10
#define INPUT_COOLDOWN_FRAMES 3
#define FEE_AMOUNT 5

static s16 gBankerValue = 0;
static s16 gBankerSelectedDigit = 0;
static s16 gBlinkTimer = 0;

extern const char* digitTextures[];
Color_RGBA8 highlightColor = { 255, 255, 0, 255 };

void UpdateBankerOverlay(PlayState* play, Gfx** gfx, s16 value, s16 selectedDigit) {
    s16 posX = 160 - 76;
    s16 posY = 180 - 20;
    s16 digits[3] = {value / 100, (value % 100) / 10, value % 10};
    s16 offsets[3] = {HUNDREDS_POSITION_OFFSET, TENS_POSITION_OFFSET, ONES_POSITION_OFFSET};
    for (int i = 0; i < 3; i++) {
        posX += offsets[i];
        s16 digit = digits[i];
        bool isSelected = selectedDigit == 2 - i;
        gDPPipeSync((*gfx)++);
        gDPLoadTextureBlock((*gfx)++, ((u8*)digitTextures[digit]), G_IM_FMT_I, G_IM_SIZ_8b, DIGIT_WIDTH, DIGIT_HEIGHT, 0,
                            G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
        Color_RGBA8 primColor = isSelected && gBlinkTimer < BLINK_DURATION ? highlightColor : (Color_RGBA8){255, 255, 255, 255};
        gDPSetPrimColor((*gfx)++, 0, 0, primColor.r, primColor.g, primColor.b, primColor.a);
        gDPSetCombineMode((*gfx)++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
        gDPSetRenderMode((*gfx)++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gSPTextureRectangle((*gfx)++, posX << 2, posY << 2, (posX + DIGIT_WIDTH) << 2, (posY + DIGIT_HEIGHT) << 2,
                            G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    }}

s16 CalculateSelectedValue(s16 value, s16 selectedDigit, bool increase, s16 min, s16 max) {
    s16 digitValues[3];
    digitValues[0] = value % 10;
    digitValues[1] = (value / 10) % 10;
    digitValues[2] = value / 100;
    if (increase) {
        digitValues[selectedDigit] = (digitValues[selectedDigit] + 1) % 10;
    } else {
        digitValues[selectedDigit] = (digitValues[selectedDigit] + 9) % 10;
    }
    s16 newValue = digitValues[2] * 100 + digitValues[1] * 10 + digitValues[0];
    return (newValue < min) ? min : (newValue > max) ? max : newValue;
}

void ProcessInput(PlayState* play, s16* value, s16* selectedDigit) {
    static s16 inputCooldownTimer = 0;
    Input* input = &play->state.input[0];
    bool playSound = false;
    if (play->msgCtx.textId != TEXT_BANKER_WITHDRAWAL_AMOUNT && play->msgCtx.textId != TEXT_BANKER_DEPOSIT_AMOUNT) {
        return;
    }
    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        Message_CloseTextbox(play);
        return;
    }
    if (inputCooldownTimer > 0) {
        inputCooldownTimer--;
        return;
    }    if (abs(input->rel.stick_y) > 30) {
        *value = CalculateSelectedValue(*value, *selectedDigit, input->rel.stick_y > 0, 0, 999);
        gBankerValue = *value;
        playSound = true;
    }
    if (abs(input->rel.stick_x) > 30) {
        *selectedDigit = (input->rel.stick_x < 0) ? (*selectedDigit + 1) % 3 : (*selectedDigit - 1 + 3) % 3;
        playSound = true;
    }
    if (playSound) {
        Audio_PlaySoundGeneral(NA_SE_SY_RUPY_COUNT, &D_801333D4, 4, &D_801333E0, &D_801333E0, &D_801333E8);
    }
    inputCooldownTimer = playSound ? INPUT_COOLDOWN_FRAMES : 0;
}

static void HandleBankerInteraction(PlayState* play, MessageContext* msgCtx) {
    static bool canContinueToAmount = false;
    static s16 OptionChoice = -1;
    uint16_t currentTextId = msgCtx->textId;

    if (msgCtx->msgMode != MSGMODE_TEXT_DONE && msgCtx->msgMode != MSGMODE_TEXT_CLOSING) {
        return;
    }

    if (!Message_ShouldAdvance(play)) {
        return;
    }

    s16 nextTextId;

    switch (currentTextId) {
        case TEXT_BEGGAR_VANILLA:
            canContinueToAmount = false;
            Message_ContinueTextbox(play, TEXT_BANKER_OPTIONS);
            break;

        case TEXT_BANKER_OPTIONS:
            if (msgCtx->choiceIndex == 0 || msgCtx->choiceIndex == 1) {
                OptionChoice = msgCtx->choiceIndex;
                if (gSaveContext.hasInterest == 0) {
                    Message_ContinueTextbox(play, TEXT_BANKER_TRANSACTION_FEE);
                } else {
                    Message_ContinueTextbox(play, TEXT_BANKER_BALANCE);
                }
            } else if (msgCtx->choiceIndex == 2) {
                Message_CloseTextbox(play);
            }
            break;

        case TEXT_BANKER_BALANCE:
            if (gSaveContext.playerBalance >= 200 && !gSaveContext.hasWarpTransfer) {
                gSaveContext.hasWarpTransfer = 1;
                Message_ContinueTextbox(play, TEXT_BANKER_REWARD_WARP_TRANSFER_INTRO);
            } else if (gSaveContext.playerBalance >= 1000 && !gSaveContext.hasInterest) {
                gSaveContext.hasInterest = 1;
                Message_ContinueTextbox(play, TEXT_BANKER_REWARD_FEE);
            } else if (gSaveContext.playerBalance >= 5000 && !gSaveContext.hasPieceOfHeart) {
                gSaveContext.hasPieceOfHeart = 1;
                Message_ContinueTextbox(play, TEXT_BANKER_REWARD_PIECE_OF_HEART);
            } else {
                nextTextId = (OptionChoice == 0) ? TEXT_BANKER_DEPOSIT_AMOUNT : TEXT_BANKER_WITHDRAWAL_AMOUNT;
                Message_ContinueTextbox(play, nextTextId);
            }
            break;

        case TEXT_BANKER_WITHDRAWAL_AMOUNT:
        case TEXT_BANKER_DEPOSIT_AMOUNT:
            if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
                Message_CloseTextbox(play);
                gBankerValue = 0;
                return;
            }
            if (gBankerValue == 0) {
                Message_ContinueTextbox(play, TEXT_BANKER_ERROR_ZERO_AMOUNT);
            } else if (currentTextId == TEXT_BANKER_WITHDRAWAL_AMOUNT) {
                if (gBankerValue + FEE_AMOUNT <= gSaveContext.playerBalance && 
                    gBankerValue + gSaveContext.rupees <= CUR_CAPACITY(UPG_WALLET)) { 
                    if (gSaveContext.hasInterest == 0) {
                        if (gBankerValue + FEE_AMOUNT > gSaveContext.playerBalance) {
                            Message_ContinueTextbox(play, TEXT_BANKER_ERROR_INSUFFICIENT_BALANCE);
                        } else {
                            Rupees_ChangeBy(gBankerValue);
                            gSaveContext.playerBalance -= (gBankerValue + FEE_AMOUNT); 
                            Message_ContinueTextbox(play, TEXT_BANKER_WITHDRAWAL_CONFIRM);
                        }
                    } else { // hasInterest == 1
                        Rupees_ChangeBy(gBankerValue);
                        gSaveContext.playerBalance -= gBankerValue;
                        Message_ContinueTextbox(play, TEXT_BANKER_WITHDRAWAL_CONFIRM);
                    }
                } else {
                    if (gBankerValue + gSaveContext.rupees > CUR_CAPACITY(UPG_WALLET)) {
                        Message_ContinueTextbox(play, TEXT_BANKER_ERROR_WALLET_FULL);
                    } else {
                        Message_ContinueTextbox(play, TEXT_BANKER_ERROR_INSUFFICIENT_BALANCE);
                    }
                }
            } else if (currentTextId == TEXT_BANKER_DEPOSIT_AMOUNT) {
                if (gBankerValue <= gSaveContext.rupees) {
                    if ((gSaveContext.playerBalance + gBankerValue) > 5000) {
                        Message_ContinueTextbox(play, TEXT_BANKER_ERROR_MAX_BALANCE);
                    } else if (gSaveContext.hasInterest == 0 && (gSaveContext.playerBalance + gBankerValue - FEE_AMOUNT) < 1) {
                        Message_ContinueTextbox(play, TEXT_BANKER_ERROR_DEPOSIT_NOT_WORTHWHILE);
                    } else {
                        if (gSaveContext.hasInterest == 0) {
                            Rupees_ChangeBy(-gBankerValue);
                            gSaveContext.playerBalance += (gBankerValue - FEE_AMOUNT); 
                        } else { // hasInterest == 1
                            Rupees_ChangeBy(-gBankerValue);
                            gSaveContext.playerBalance += gBankerValue;
                        }
                        Message_ContinueTextbox(play, TEXT_BANKER_DEPOSIT_CONFIRM);
                    }
                } else {
                    Message_ContinueTextbox(play, TEXT_BANKER_ERROR_INSUFFICIENT_BALANCE);
                }
            }
            break;

        case TEXT_BANKER_WITHDRAWAL_CONFIRM:
        case TEXT_BANKER_DEPOSIT_CONFIRM:
            gBankerValue = 0;
            canContinueToAmount = true;
            if (gSaveContext.playerBalance >= 200 && !gSaveContext.hasWarpTransfer) {
                gSaveContext.hasWarpTransfer = 1;
                Message_ContinueTextbox(play, TEXT_BANKER_REWARD_WARP_TRANSFER_INTRO);
            } else if (gSaveContext.playerBalance >= 1000 && !gSaveContext.hasInterest) {
                gSaveContext.hasInterest = 1;
                Message_ContinueTextbox(play, TEXT_BANKER_REWARD_FEE);
            } else if (gSaveContext.playerBalance >= 5000 && !gSaveContext.hasPieceOfHeart) {
                gSaveContext.hasPieceOfHeart = 1;
                Message_ContinueTextbox(play, TEXT_BANKER_REWARD_PIECE_OF_HEART);
            }
            break;

        case TEXT_BANKER_REWARD_WARP_TRANSFER_INTRO:
            Message_ContinueTextbox(play, TEXT_BANKER_REWARD_WARP_TRANSFER_ITEM);
            break;

        case TEXT_BANKER_REWARD_WARP_TRANSFER_ITEM:
            Message_ContinueTextbox(play, TEXT_BANKER_REWARD_WARP_TRANSFER_LORE_1);
            break;

        case TEXT_BANKER_REWARD_WARP_TRANSFER_LORE_1:
            Message_ContinueTextbox(play, TEXT_BANKER_REWARD_WARP_TRANSFER_LORE_2);
            break;

        case TEXT_BANKER_REWARD_WARP_TRANSFER_LORE_2:
            Message_ContinueTextbox(play, TEXT_BANKER_REWARD_WARP_TRANSFER_LORE_3);
            break;

        case TEXT_BANKER_REWARD_WARP_TRANSFER_LORE_3:
            if (gSaveContext.playerBalance >= 1000 && !gSaveContext.hasInterest) {
                gSaveContext.hasInterest = 1;
                Message_ContinueTextbox(play, TEXT_BANKER_REWARD_FEE);
            } else if (canContinueToAmount) {
                canContinueToAmount = false;
            } else {
                nextTextId = (OptionChoice == 0) ? TEXT_BANKER_DEPOSIT_AMOUNT : TEXT_BANKER_WITHDRAWAL_AMOUNT;
                Message_ContinueTextbox(play, nextTextId);
            }
            break;

        case TEXT_BANKER_REWARD_FEE:
            if (gSaveContext.playerBalance >= 5000 && !gSaveContext.hasPieceOfHeart) {
                gSaveContext.hasPieceOfHeart = 1;
                Message_ContinueTextbox(play, TEXT_BANKER_REWARD_PIECE_OF_HEART);
            } else if (canContinueToAmount) {
                canContinueToAmount = false;
            } else {
                nextTextId = (OptionChoice == 0) ? TEXT_BANKER_DEPOSIT_AMOUNT : TEXT_BANKER_WITHDRAWAL_AMOUNT;
                Message_ContinueTextbox(play, nextTextId);
            }
            break;

        case TEXT_BANKER_REWARD_PIECE_OF_HEART:
            Message_ContinueTextbox(play, TEXT_HEART_PIECE);
            Audio_PlaySoundGeneral(NA_SE_SY_PIECE_OF_HEART, &D_801333D4, 4, &D_801333E0, &D_801333E0, &D_801333E8);
            Item_Give(play, ITEM_HEART_PIECE);
            gSaveContext.healthAccumulator = gSaveContext.healthCapacity - gSaveContext.health;
            break;

        case TEXT_HEART_PIECE:
            if (canContinueToAmount) {
                canContinueToAmount = false;
            } else {
                nextTextId = (OptionChoice == 0) ? TEXT_BANKER_DEPOSIT_AMOUNT : TEXT_BANKER_WITHDRAWAL_AMOUNT;
                Message_ContinueTextbox(play, nextTextId);
            }
            break;

        case TEXT_BANKER_ERROR_ZERO_AMOUNT:
            Message_CloseTextbox(play);
            break;

        case TEXT_BANKER_ERROR_INSUFFICIENT_BALANCE:
            Message_ContinueTextbox(play, (currentTextId == TEXT_BANKER_WITHDRAWAL_AMOUNT) ? TEXT_BANKER_WITHDRAWAL_AMOUNT : TEXT_BANKER_DEPOSIT_AMOUNT);
            break;

        case TEXT_BANKER_ERROR_WALLET_FULL:
            Message_ContinueTextbox(play, TEXT_BANKER_WITHDRAWAL_AMOUNT);
            break;

        case TEXT_BANKER_ERROR_MAX_BALANCE:
            Message_ContinueTextbox(play, TEXT_BANKER_DEPOSIT_AMOUNT);
            break;

        case TEXT_BANKER_ERROR_DEPOSIT_NOT_WORTHWHILE:
            Message_ContinueTextbox(play, TEXT_BANKER_DEPOSIT_AMOUNT);
            break;

        case TEXT_BANKER_TRANSACTION_FEE:
            if (msgCtx->choiceIndex == 0) { // Player chooses 'Yes', proceed with transaction
                Message_ContinueTextbox(play, TEXT_BANKER_BALANCE);
            } else if (msgCtx->choiceIndex == 1) { // Player chooses 'No', does not want to proceed due to fee
                Message_CloseTextbox(play);
            }
            break;
            
        default:
            break;
    }
}

void DrawBankerOverlay(PlayState* play, Gfx** gfx) {
    UpdateBankerOverlay(play, gfx, gBankerValue, gBankerSelectedDigit);
}

void BankerMain(PlayState* play, GraphicsContext* gfxCtx) {
    if (play->msgCtx.msgMode == MSGMODE_NONE) {
        return;
    }
    ProcessInput(play, &gBankerValue, &gBankerSelectedDigit);
    gBlinkTimer = (gBlinkTimer + 1) % (BLINK_DURATION * 2);
    if (play->msgCtx.textId == TEXT_BANKER_WITHDRAWAL_AMOUNT || play->msgCtx.textId == TEXT_BANKER_DEPOSIT_AMOUNT) {
        Gfx** gfx = &gfxCtx->overlay.p;
        UpdateBankerOverlay(play, gfx, gBankerValue, gBankerSelectedDigit);
    }
    HandleBankerInteraction(play, &play->msgCtx);
}
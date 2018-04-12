/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_view_controller.h"

#include "passport/passport_form_controller.h"
#include "passport/passport_panel_edit_document.h"
#include "passport/passport_panel_edit_contact.h"
#include "passport/passport_panel_controller.h"
#include "lang/lang_keys.h"

namespace Passport {
namespace {

std::map<Value::Type, Scope::Type> ScopeTypesMap() {
	return {
		{ Value::Type::PersonalDetails, Scope::Type::Identity },
		{ Value::Type::Passport, Scope::Type::Identity },
		{ Value::Type::DriverLicense, Scope::Type::Identity },
		{ Value::Type::IdentityCard, Scope::Type::Identity },
		{ Value::Type::Address, Scope::Type::Address },
		{ Value::Type::UtilityBill, Scope::Type::Address },
		{ Value::Type::BankStatement, Scope::Type::Address },
		{ Value::Type::RentalAgreement, Scope::Type::Address },
		{ Value::Type::Phone, Scope::Type::Phone },
		{ Value::Type::Email, Scope::Type::Email },
	};
}

Scope::Type ScopeTypeForValueType(Value::Type type) {
	static const auto map = ScopeTypesMap();
	const auto i = map.find(type);
	Assert(i != map.end());
	return i->second;
}

std::map<Scope::Type, Value::Type> ScopeFieldsMap() {
	return {
		{ Scope::Type::Identity, Value::Type::PersonalDetails },
		{ Scope::Type::Address, Value::Type::Address },
		{ Scope::Type::Phone, Value::Type::Phone },
		{ Scope::Type::Email, Value::Type::Email },
	};
}

Value::Type FieldsTypeForScopeType(Scope::Type type) {
	static const auto map = ScopeFieldsMap();
	const auto i = map.find(type);
	Assert(i != map.end());
	return i->second;
}

} // namespace

Scope::Scope(Type type, not_null<const Value*> fields)
: type(type)
, fields(fields) {
}

std::vector<Scope> ComputeScopes(
		not_null<const FormController*> controller) {
	auto scopes = std::map<Scope::Type, Scope>();
	const auto &form = controller->form();
	const auto findValue = [&](const Value::Type type) {
		const auto i = form.values.find(type);
		Assert(i != form.values.end());
		return &i->second;
	};
	for (const auto type : form.request) {
		const auto scopeType = ScopeTypeForValueType(type);
		const auto fieldsType = FieldsTypeForScopeType(scopeType);
		const auto [i, ok] = scopes.emplace(
			scopeType,
			Scope(scopeType, findValue(fieldsType)));
		i->second.selfieRequired = (scopeType == Scope::Type::Identity)
			&& form.identitySelfieRequired;
		const auto alreadyIt = ranges::find(
			i->second.documents,
			type,
			[](not_null<const Value*> value) { return value->type; });
		if (alreadyIt != end(i->second.documents)) {
			LOG(("API Error: Value type %1 multiple times in request."
				).arg(int(type)));
			continue;
		} else if (type != fieldsType) {
			i->second.documents.push_back(findValue(type));
		}
	}
	auto result = std::vector<Scope>();
	result.reserve(scopes.size());
	for (auto &[type, scope] : scopes) {
		result.push_back(std::move(scope));
	}
	return result;
}

QString ComputeScopeRowReadyString(const Scope &scope) {
	switch (scope.type) {
	case Scope::Type::Identity:
	case Scope::Type::Address: {
		auto list = QStringList();
		const auto &fields = scope.fields->data.parsed.fields;
		const auto document = [&]() -> const Value* {
			for (const auto &document : scope.documents) {
				if (!document->scans.empty()) {
					return document;
				}
			}
			return nullptr;
		}();
		if (document && scope.documents.size() > 1) {
			list.push_back([&] {
				switch (document->type) {
				case Value::Type::Passport:
					return lang(lng_passport_identity_passport);
				case Value::Type::DriverLicense:
					return lang(lng_passport_identity_license);
				case Value::Type::IdentityCard:
					return lang(lng_passport_identity_card);
				case Value::Type::BankStatement:
					return lang(lng_passport_address_statement);
				case Value::Type::UtilityBill:
					return lang(lng_passport_address_bill);
				case Value::Type::RentalAgreement:
					return lang(lng_passport_address_agreement);
				default: Unexpected("Files type in ComputeScopeRowReadyString.");
				}
			}());
		}
		if (document
			&& (document->scans.empty()
				|| (scope.selfieRequired && !document->selfie))) {
			return QString();
		}
		const auto scheme = GetDocumentScheme(scope.type);
		for (const auto &row : scheme.rows) {
			const auto format = row.format;
			if (row.valueClass == EditDocumentScheme::ValueClass::Fields) {
				const auto i = fields.find(row.key);
				if (i == end(fields)) {
					return QString();
				} else if (row.validate && !row.validate(i->second)) {
					return QString();
				}
				list.push_back(format ? format(i->second) : i->second);
			} else if (!document) {
				return QString();
			} else {
				const auto i = document->data.parsed.fields.find(row.key);
				if (i == end(document->data.parsed.fields)) {
					return QString();
				} else if (row.validate && !row.validate(i->second)) {
					return QString();
				}
				list.push_back(i->second);
			}
		}
		return list.join(", ");
	} break;
	case Scope::Type::Phone:
	case Scope::Type::Email: {
		const auto format = GetContactScheme(scope.type).format;
		const auto &fields = scope.fields->data.parsed.fields;
		const auto i = fields.find("value");
		return (i != end(fields))
			? (format ? format(i->second) : i->second)
			: QString();
	} break;
	}
	Unexpected("Scope type in ComputeScopeRowReadyString.");
}

ScopeRow ComputeScopeRow(const Scope &scope) {
	switch (scope.type) {
	case Scope::Type::Identity:
		if (scope.documents.empty()) {
			return {
				lang(lng_passport_personal_details),
				lang(lng_passport_personal_details_enter),
				ComputeScopeRowReadyString(scope)
			};
		} else if (scope.documents.size() == 1) {
			switch (scope.documents.front()->type) {
			case Value::Type::Passport:
				return {
					lang(lng_passport_identity_passport),
					lang(lng_passport_identity_passport_upload),
					ComputeScopeRowReadyString(scope)
				};
			case Value::Type::IdentityCard:
				return {
					lang(lng_passport_identity_card),
					lang(lng_passport_identity_card_upload),
					ComputeScopeRowReadyString(scope)
				};
			case Value::Type::DriverLicense:
				return {
					lang(lng_passport_identity_license),
					lang(lng_passport_identity_license_upload),
					ComputeScopeRowReadyString(scope)
				};
			default: Unexpected("Identity type in ComputeScopeRow.");
			}
		}
		return {
			lang(lng_passport_identity_title),
			lang(lng_passport_identity_description),
			ComputeScopeRowReadyString(scope)
		};
	case Scope::Type::Address:
		if (scope.documents.empty()) {
			return {
				lang(lng_passport_address),
				lang(lng_passport_address_enter),
				ComputeScopeRowReadyString(scope)
			};
		} else if (scope.documents.size() == 1) {
			switch (scope.documents.front()->type) {
			case Value::Type::BankStatement:
				return {
					lang(lng_passport_address_statement),
					lang(lng_passport_address_statement_upload),
					ComputeScopeRowReadyString(scope)
				};
			case Value::Type::UtilityBill:
				return {
					lang(lng_passport_address_bill),
					lang(lng_passport_address_bill_upload),
					ComputeScopeRowReadyString(scope)
				};
			case Value::Type::RentalAgreement:
				return {
					lang(lng_passport_address_agreement),
					lang(lng_passport_address_agreement_upload),
					ComputeScopeRowReadyString(scope)
				};
			default: Unexpected("Address type in ComputeScopeRow.");
			}
		}
		return {
			lang(lng_passport_address_title),
			lang(lng_passport_address_description),
			ComputeScopeRowReadyString(scope)
		};
	case Scope::Type::Phone:
		return {
			lang(lng_passport_phone_title),
			lang(lng_passport_phone_description),
			ComputeScopeRowReadyString(scope)
		};
	case Scope::Type::Email:
		return {
			lang(lng_passport_email_title),
			lang(lng_passport_email_description),
			ComputeScopeRowReadyString(scope)
		};
	default: Unexpected("Scope type in ComputeScopeRow.");
	}
}

} // namespace Passport
